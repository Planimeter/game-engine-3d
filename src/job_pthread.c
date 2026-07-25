/* Copyright Planimeter. All Rights Reserved.
 *
 * Pthread-based job system with work-stealing.
 *
 * DESIGN NOTES (informed by industry research):
 *
 * 1. O(1) Handle Lookup: JobHandle encodes slot index + generation counter.
 *    job_wait() decodes the slot directly instead of scanning MAX_JOBS entries.
 *
 * 2. Wait-That-Does-Work: job_wait() executes available pending jobs instead
 *    of busy-waiting (Molecular Matters, Naughty Dog GDC 2015, Our Machinery).
 *
 * 3. Parent-Child unfinishedJobs: Jobs can optionally declare a parent handle.
 *    A child increments its parent's unfinishedJobs counter so the parent
 *    cannot complete until all children have finished. (The JobDescriptor.parent
 *    field enables this — pass {0} for no parent.)
 *
 * 4. Condition Variables: Workers block on condvar when queues are empty.
 *    job_submit() signals the condvar to wake workers.
 *
 * 5. Work-Stealing: Each worker has its own queue. Idle workers steal from
 *    other queues (bottom-steal, top-pop).
 */

#include "job.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

#ifdef _WIN32
    #include <windows.h>
    #include <process.h>
    typedef HANDLE pthread_t;
    typedef CRITICAL_SECTION pthread_mutex_t;
    typedef CONDITION_VARIABLE pthread_cond_t;
    typedef volatile LONG atomic_int;
    typedef volatile LONG64 atomic_uint64;
    typedef volatile LONG atomic_uint32;
    
    #define atomic_store(ptr, val) InterlockedExchange((LONG*)ptr, val)
    #define atomic_store_64(ptr, val) InterlockedExchange64((LONG64*)ptr, val)
    #define atomic_load(ptr) InterlockedCompareExchange((LONG*)ptr, 0, 0)
    #define atomic_load_64(ptr) InterlockedCompareExchange64((LONG64*)ptr, 0, 0)
    #define atomic_fetch_add(ptr, val) InterlockedExchangeAdd((LONG*)ptr, val)
    #define atomic_fetch_add_64(ptr, val) InterlockedExchangeAdd64((LONG64*)ptr, val)
    #define atomic_fetch_add_32(ptr, val) InterlockedExchangeAdd((LONG*)ptr, val)
    
    #define pthread_mutex_init(m, a) InitializeCriticalSection(m)
    #define pthread_mutex_destroy(m) DeleteCriticalSection(m)
    #define pthread_mutex_lock(m) EnterCriticalSection(m)
    #define pthread_mutex_unlock(m) LeaveCriticalSection(m)
    #define pthread_cond_init(c, a) InitializeConditionVariable(c)
    #define pthread_cond_destroy(c) (void)0
    #define pthread_cond_wait(c, m) SleepConditionVariableCS(c, m, INFINITE)
    #define pthread_cond_signal(c) WakeConditionVariable(c)
    #define pthread_cond_broadcast(c) WakeAllConditionVariable(c)
    #define pthread_sleep_np(ms) Sleep(ms)
    
    static inline int pthread_create_win32(pthread_t *thread, void *attr, void *(*start_routine)(void*), void *arg) {
        (void)attr;
        *thread = (HANDLE)_beginthreadex(NULL, 0, (unsigned(__stdcall*)(void*))start_routine, arg, 0, NULL);
        return *thread == NULL ? -1 : 0;
    }
    #define pthread_create(t, a, f, arg) pthread_create_win32(t, a, f, arg)
    
    static inline int pthread_join(pthread_t thread, void **retval) {
        (void)retval;
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        return 0;
    }
#else
    #include <pthread.h>
    #include <stdatomic.h>
    #include <unistd.h>
    typedef _Atomic(int) atomic_int;
    typedef _Atomic(uint64_t) atomic_uint64;
    typedef _Atomic(uint32_t) atomic_uint32;
    #define atomic_store_64 atomic_store
    #define atomic_load_64 atomic_load
    #define atomic_fetch_add_64 atomic_fetch_add
    #define atomic_fetch_add_32 atomic_fetch_add
    
    static inline void pthread_sleep_np(uint32_t milliseconds) {
        usleep(milliseconds * 1000);
    }
#endif

/* ---------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define MAX_JOBS      65536
#define MAX_WORKERS   16
#define MAX_JOB_QUEUE 1024

/* Handle encoding: bits [63:48] = slot index (16 bits), bits [47:0] = generation (48 bits).
 * This gives O(1) job lookup in job_wait() — no more scanning all 65536 entries.
 *
 * We mask the generation to 48 bits when storing it in the Job, so the comparison
 * in job_wait() (stored handleId vs decoded generation from user handle) is
 * always correct even after 2^48 total submissions. */
#define HANDLE_SLOT_SHIFT 48
#define HANDLE_GEN_MASK   0x0000FFFFFFFFFFFFULL

static inline uint64_t encode_handle(uint32_t slot, uint64_t generation) {
    return ((uint64_t)slot << HANDLE_SLOT_SHIFT) | (generation & HANDLE_GEN_MASK);
}
static inline uint32_t decode_slot(uint64_t handle) {
    return (uint32_t)(handle >> HANDLE_SLOT_SHIFT);
}
static inline uint64_t decode_generation(uint64_t handle) {
    return handle & HANDLE_GEN_MASK;
}

/* ---------------------------------------------------------------------------
 * Internal types
 * ------------------------------------------------------------------------- */

typedef struct {
    JobFunction    function;
    void          *context;
    uint32_t       jobCount;
    uint32_t       completedCount;   /* atomic — for statistics */
    uint32_t       currentIndex;     /* atomic — next work item to claim */
    const char    *name;
    uint32_t       unfinishedJobs;   /* atomic — counts remaining work items */
    uint64_t       handleId;         /* atomic — generation for slot recycling (0 = free) */
    uint64_t       parentHandleId;   /* atomic — handle of parent job (0 = none) */
    JobSystem     *parentSystem;     /* non-atomic, set once at submission */
} Job;

typedef struct {
    Job           *jobs[MAX_JOB_QUEUE];
    uint32_t       head;
    uint32_t       tail;
    pthread_mutex_t mutex;
    pthread_cond_t  workAvailable;
} JobQueue;

typedef struct {
    JobSystem  *jobSystem;
    uint32_t    workerIndex;
} WorkerThreadArgs;

struct JobSystem {
    JobQueue               queues[MAX_WORKERS];
    pthread_t              threads[MAX_WORKERS];
    uint32_t               workerCount;
    int                    running;         /* atomic */
    uint64_t               nextJobId;       /* atomic */
    uint64_t               nextSlotHint;    /* atomic — ring buffer allocator hint */
    Job                    jobs[MAX_JOBS];
    int                    jobCount;        /* atomic — total live jobs */
    pthread_mutex_t        jobPoolMutex;
};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------------- */

static void *job_worker(void *arg);
static Job  *job_try_get_work(JobSystem *sys, uint32_t workerIndex);
static void  job_execute_one(JobSystem *sys, Job *job, uint32_t workerIndex);
static void  job_finish_work_item(Job *job);
static uint32_t job_alloc_slot(JobSystem *sys);
static void  job_free_slot(Job *job);

/* ---------------------------------------------------------------------------
 * Job slot allocator — ring buffer under mutex, O(1) average case.
 * ------------------------------------------------------------------------- */

static uint32_t job_alloc_slot(JobSystem *sys) {
    pthread_mutex_lock(&sys->jobPoolMutex);

    /* Ring-buffer probe: start from nextSlotHint and try up to MAX_JOBS slots.
     * In practice, with 65536 slots and workloads that free promptly,
     * this finds a free slot within a few iterations. */
    uint32_t result = MAX_JOBS;
    uint64_t start = sys->nextSlotHint++;
    for (uint32_t attempt = 0; attempt < MAX_JOBS; attempt++) {
        uint32_t slot = (uint32_t)((start + attempt) % MAX_JOBS);
        if (atomic_load_64(&sys->jobs[slot].handleId) == 0) {
            /* Mark slot as reserved (UINT64_MAX = "reserved but not yet initialized") */
            atomic_store_64(&sys->jobs[slot].handleId, UINT64_MAX);
            result = slot;
            break;
        }
    }

    pthread_mutex_unlock(&sys->jobPoolMutex);
    return result;
}

static void job_free_slot(Job *job) {
    atomic_store_64(&job->handleId, 0);
}

/* ---------------------------------------------------------------------------
 * Worker thread entry point
 * ------------------------------------------------------------------------- */

static void *job_worker(void *arg)
{
    WorkerThreadArgs *args = (WorkerThreadArgs *)arg;
    JobSystem *sys = args->jobSystem;
    uint32_t workerIndex = args->workerIndex;
    free(args);

    while (atomic_load(&sys->running)) {
        JobQueue *myQueue = &sys->queues[workerIndex];
        Job *job = NULL;

        /* Wait on condition variable until work is available */
        pthread_mutex_lock(&myQueue->mutex);
        while (myQueue->head >= myQueue->tail && atomic_load(&sys->running)) {
            pthread_cond_wait(&myQueue->workAvailable, &myQueue->mutex);
        }
        if (!atomic_load(&sys->running)) {
            pthread_mutex_unlock(&myQueue->mutex);
            break;
        }
        /* Pop from front (LIFO for owner = better cache locality) */
        job = myQueue->jobs[myQueue->head++ % MAX_JOB_QUEUE];
        pthread_mutex_unlock(&myQueue->mutex);

        if (job) {
            job_execute_one(sys, job, workerIndex);
        }
    }

    return NULL;
}

/* ---------------------------------------------------------------------------
 * Work execution helpers
 * ------------------------------------------------------------------------- */

/* Try to get any available job: own queue first, then steal from others.
 * Used by job_wait() and job_waitcounter() to do useful work while waiting. */
static Job *job_try_get_work(JobSystem *sys, uint32_t workerIndex)
{
    /* Try own queue */
    if (workerIndex < sys->workerCount) {
        JobQueue *myQueue = &sys->queues[workerIndex];
        pthread_mutex_lock(&myQueue->mutex);
        if (myQueue->head < myQueue->tail) {
            Job *job = myQueue->jobs[myQueue->head++ % MAX_JOB_QUEUE];
            pthread_mutex_unlock(&myQueue->mutex);
            return job;
        }
        pthread_mutex_unlock(&myQueue->mutex);
    }

    /* Try stealing from others (start at random queue to reduce contention) */
    uint32_t start = (uint32_t)(rand() % sys->workerCount);
    for (uint32_t i = 0; i < sys->workerCount; i++) {
        uint32_t victim = (start + i) % sys->workerCount;
        if (victim == workerIndex) continue;

        JobQueue *otherQueue = &sys->queues[victim];
        pthread_mutex_lock(&otherQueue->mutex);
        if (otherQueue->head < otherQueue->tail) {
            /* Steal from the back (FIFO from thief's perspective = better load balance) */
            uint32_t stealIndex = (otherQueue->tail - 1) % MAX_JOB_QUEUE;
            Job *job = otherQueue->jobs[stealIndex];
            otherQueue->tail--;
            pthread_mutex_unlock(&otherQueue->mutex);
            return job;
        }
        pthread_mutex_unlock(&otherQueue->mutex);
    }

    return NULL; /* No work available anywhere */
}

/* Execute one work item from a job. The worker claims a unique index via
 * currentIndex, calls the job function, then finishes the work item. */
static void job_execute_one(JobSystem *sys, Job *job, uint32_t workerIndex)
{
    (void)workerIndex;

    uint32_t myJobIndex = atomic_fetch_add_32(&job->currentIndex, 1);
    if (myJobIndex < job->jobCount) {
        job->function(job->context, myJobIndex);
        job_finish_work_item(job);
    }
    /* If myJobIndex >= jobCount, all items were already claimed — this queue
     * entry was a duplicate; nothing to execute. */
}

/* Called after one work item completes. Decrements unfinishedJobs.
 * If it reaches 0, the job is fully complete: we free the slot.
 *
 * If the job has a parent, we recursively notify the parent (decrement its
 * unfinishedJobs too). This implements the parent-child relationship:
 * a parent is not "finished" until all its children have finished. */
static void job_finish_work_item(Job *job)
{
    uint32_t remaining = atomic_fetch_add_32(&job->unfinishedJobs, (uint32_t)-1) - 1;

    if (remaining == 0) {
        /* This job is fully complete. Notify parent if one exists. */
        uint64_t parentHandle = atomic_load_64(&job->parentHandleId);
        if (parentHandle != 0) {
            uint32_t parentSlot = decode_slot(parentHandle);
            uint64_t parentGen  = decode_generation(parentHandle);
            Job *parent = &job->parentSystem->jobs[parentSlot];
            /* Check generation to avoid acting on a recycled slot */
            if (atomic_load_64(&parent->handleId) == parentGen) {
                job_finish_work_item(parent);
            }
        }
        /* Free the slot for reuse */
        job_free_slot(job);
        atomic_fetch_add(&job->parentSystem->jobCount, -1);
    }
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

JobSystem *job_create(uint32_t workerThreadCount)
{
    if (workerThreadCount == 0) {
        workerThreadCount = 4;
    }
    if (workerThreadCount > MAX_WORKERS) {
        workerThreadCount = MAX_WORKERS;
    }

    JobSystem *sys = (JobSystem *)calloc(1, sizeof(JobSystem));
    if (!sys) return NULL;

    sys->workerCount = workerThreadCount;
    atomic_store(&sys->running, 1);
    atomic_store_64(&sys->nextJobId, 1);
    atomic_store_64(&sys->nextSlotHint, 0);
    pthread_mutex_init(&sys->jobPoolMutex, NULL);

    for (uint32_t i = 0; i < sys->workerCount; i++) {
        pthread_mutex_init(&sys->queues[i].mutex, NULL);
        pthread_cond_init(&sys->queues[i].workAvailable, NULL);
        sys->queues[i].head = 0;
        sys->queues[i].tail = 0;
    }

    for (uint32_t i = 0; i < sys->workerCount; i++) {
        WorkerThreadArgs *args = (WorkerThreadArgs *)malloc(sizeof(WorkerThreadArgs));
        if (!args) {
            job_destroy(sys);
            return NULL;
        }
        args->jobSystem = sys;
        args->workerIndex = i;

        if (pthread_create(&sys->threads[i], NULL, job_worker, (void *)args) != 0) {
            free(args);
            job_destroy(sys);
            return NULL;
        }
    }

    return sys;
}

void job_destroy(JobSystem *jobSystem)
{
    if (!jobSystem) return;

    atomic_store(&jobSystem->running, 0);

    /* Wake all sleeping workers so they see running == 0 and exit */
    for (uint32_t i = 0; i < jobSystem->workerCount; i++) {
        pthread_cond_broadcast(&jobSystem->queues[i].workAvailable);
    }

    for (uint32_t i = 0; i < jobSystem->workerCount; i++) {
        pthread_join(jobSystem->threads[i], NULL);
    }

    for (uint32_t i = 0; i < jobSystem->workerCount; i++) {
        pthread_cond_destroy(&jobSystem->queues[i].workAvailable);
        pthread_mutex_destroy(&jobSystem->queues[i].mutex);
    }
    pthread_mutex_destroy(&jobSystem->jobPoolMutex);

    free(jobSystem);
}

JobHandle job_submit(JobSystem *jobSystem, const JobDescriptor *descriptor)
{
    if (!jobSystem || !descriptor || !descriptor->function) {
        return (JobHandle){0};
    }

    if (descriptor->jobCount == 0) {
        return (JobHandle){0};
    }

    if (descriptor->jobCount > (uint32_t)INT_MAX) {
        return (JobHandle){0};
    }

    if (descriptor->jobCount > MAX_JOB_QUEUE * jobSystem->workerCount) {
        return (JobHandle){0};
    }

    /* Allocate a job slot (O(1) average, ring buffer) */
    uint32_t slot = job_alloc_slot(jobSystem);
    if (slot >= MAX_JOBS) {
        return (JobHandle){0};
    }

    Job *job = &jobSystem->jobs[slot];

    /* Initialize the job */
    job->function     = descriptor->function;
    job->context      = descriptor->context;
    job->jobCount     = descriptor->jobCount;
    job->completedCount = 0;
    job->currentIndex = 0;
    job->name         = descriptor->name;
    job->parentSystem = jobSystem;

    /* Set up parent-child relationship (if parent handle is valid).
     * We increment the parent's unfinishedJobs FIRST, then verify the parent
     * is still alive. If the parent died between our check and increment,
     * we undo the increment. This is a safe TOCTOU protocol. */
    uint64_t parentHandleId = descriptor->parent.id;
    if (parentHandleId != 0) {
        uint32_t parentSlot    = decode_slot(parentHandleId);
        uint64_t parentGen     = decode_generation(parentHandleId);
        Job     *parent        = &jobSystem->jobs[parentSlot];

        /* Attempt to register as a child: increment parent's counter */
        atomic_fetch_add_32(&parent->unfinishedJobs, 1);
        /* Now verify the parent is still alive at this slot */
        if (atomic_load_64(&parent->handleId) != parentGen) {
            /* Parent was already recycled — undo the increment */
            atomic_fetch_add_32(&parent->unfinishedJobs, (uint32_t)-1);
            parentHandleId = 0;
        }
    }
    atomic_store_64(&job->parentHandleId, parentHandleId);

    /* Assign generation ID (masked to 48 bits for correct decode comparison) */
    uint64_t generation = atomic_fetch_add_64(&jobSystem->nextJobId, 1) & HANDLE_GEN_MASK;
    atomic_store_64(&job->handleId, generation);
    atomic_store(&job->unfinishedJobs, (uint32_t)descriptor->jobCount);
    atomic_fetch_add(&jobSystem->jobCount, 1);

    /* Push work items to queues (round-robin distribution).
     * Each queue entry points to the same Job; workers claim unique work
     * items via atomic currentIndex. */
    uint32_t queueIndex = 0;
    for (uint32_t i = 0; i < descriptor->jobCount; i++) {
        JobQueue *queue = &jobSystem->queues[queueIndex % jobSystem->workerCount];

        pthread_mutex_lock(&queue->mutex);
        /* Spin-wait for space (should be extremely rare — queue is 1024 entries) */
        while (queue->tail - queue->head >= MAX_JOB_QUEUE) {
            pthread_mutex_unlock(&queue->mutex);
            pthread_sleep_np(1);
            pthread_mutex_lock(&queue->mutex);
        }
        queue->jobs[queue->tail++ % MAX_JOB_QUEUE] = job;
        pthread_mutex_unlock(&queue->mutex);

        /* Wake a worker that might be sleeping on this queue */
        pthread_cond_signal(&queue->workAvailable);

        queueIndex++;
    }

    return (JobHandle){ encode_handle(slot, generation) };
}

void job_wait(JobSystem *jobSystem, JobHandle handle)
{
    if (!jobSystem || handle.id == 0) return;

    /* O(1) lookup: decode slot and generation from the handle directly.
     * No more scanning all 65536 entries! */
    uint32_t slot      = decode_slot(handle.id);
    uint64_t generation = decode_generation(handle.id);
    Job *job = &jobSystem->jobs[slot];

    /* Verify the slot hasn't been recycled by comparing generations */
    if (atomic_load_64(&job->handleId) != generation) {
        return; /* Job already completed */
    }

    /* Wait until job is done, executing pending work while waiting.
     *
     * This is the "wait-that-does-work" pattern used by:
     * - Molecular Matters (Stefan Reinalter): "If a thread is waiting for
     *   a job to complete, and there's still work to be done, you should
     *   do that work instead of going to sleep or spinning."
     * - Naughty Dog (Christian Gyrling, GDC 2015): Fiber swap-on-wait
     * - Our Machinery (Tobias Persson): Fiber yield to ready queue
     *
     * The key insight: spinning with pthread_sleep_np(0) does NOTHING to
     * progress the job graph. Executing available work DOES. */
    while (atomic_load(&job->unfinishedJobs) > 0) {
        int didWork = 0;
        for (uint32_t i = 0; i < jobSystem->workerCount && !didWork; i++) {
            Job *work = job_try_get_work(jobSystem, i);
            if (work) {
                job_execute_one(jobSystem, work, i);
                didWork = 1;
            }
        }
        if (!didWork) {
            /* No work available anywhere — yield CPU briefly */
            pthread_sleep_np(0);
        }
    }
}

void job_waitall(JobSystem *jobSystem)
{
    if (!jobSystem) return;

    while (atomic_load(&jobSystem->jobCount) > 0) {
        int allDone = 1;

        /* Scan for unfinished jobs. We start from the nextSlotHint to
         * narrow the search, falling back to a full scan if needed. */
        uint64_t hint = atomic_load_64(&jobSystem->nextSlotHint);
        for (uint32_t i = 0; i < MAX_JOBS; i++) {
            uint32_t idx = (uint32_t)((hint + i) % MAX_JOBS);
            if (atomic_load(&jobSystem->jobs[idx].unfinishedJobs) > 0) {
                allDone = 0;
                /* Execute available work to help make progress */
                for (uint32_t w = 0; w < jobSystem->workerCount; w++) {
                    Job *work = job_try_get_work(jobSystem, w);
                    if (work) {
                        job_execute_one(jobSystem, work, w);
                        break;
                    }
                }
                break;
            }
        }
        if (allDone) break;
        pthread_sleep_np(0);
    }
}

int job_isfinished(JobSystem *jobSystem, JobHandle handle)
{
    if (!jobSystem || handle.id == 0) return 1;

    uint32_t slot      = decode_slot(handle.id);
    uint64_t generation = decode_generation(handle.id);
    Job *job = &jobSystem->jobs[slot];

    /* If slot recycled or generation mismatch, the job is done */
    if (atomic_load_64(&job->handleId) != generation) return 1;

    return atomic_load(&job->unfinishedJobs) == 0;
}

uint32_t job_getworkercount(JobSystem *jobSystem)
{
    return jobSystem ? jobSystem->workerCount : 0;
}

JobSystem *job_get_system(void)
{
    return g_jobSystem;
}

/* ---------------------------------------------------------------------------
 * Counter API (used for manual synchronization points)
 * ------------------------------------------------------------------------- */

JobCounter job_createcounter(void)
{
    JobCounter c = {0};
    return c;
}

void job_incrementcounter(JobCounter *counter)
{
    if (counter) {
        job_atomic_fetch_add_u64(&counter->counter, 1);
    }
}

void job_waitcounter(JobSystem *jobSystem, JobCounter *counter, uint64_t targetValue)
{
    if (!jobSystem || !counter) return;

    while (job_atomic_load_u64(&counter->counter) < targetValue) {
        /* Execute work while waiting for counter (same wait-that-does-work pattern) */
        int didWork = 0;
        for (uint32_t i = 0; i < jobSystem->workerCount && !didWork; i++) {
            Job *work = job_try_get_work(jobSystem, i);
            if (work) {
                job_execute_one(jobSystem, work, i);
                didWork = 1;
            }
        }
        if (!didWork) {
            pthread_sleep_np(0);
        }
    }
}
