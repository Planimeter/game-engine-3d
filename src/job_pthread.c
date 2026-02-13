/* Copyright Planimeter. All Rights Reserved. */

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

#define MAX_JOBS 65536
#define MAX_WORKERS 16
#define MAX_JOB_QUEUE 1024

static void *job_worker(void *arg);

typedef struct {
    JobFunction function;
    void        *context;
    uint32_t     jobCount;
    atomic_uint32 completedCount;
    atomic_uint32 currentIndex;
    const char  *name;
    atomic_int   refCount;
    atomic_uint64 handleId;
} Job;

typedef struct {
    Job       *jobs[MAX_JOB_QUEUE];
    uint32_t   head;
    uint32_t   tail;
    pthread_mutex_t mutex;
} JobQueue;

typedef struct {
    JobSystem  *jobSystem;
    uint32_t    workerIndex;
} WorkerThreadArgs;

struct JobSystem {
    JobQueue               queues[MAX_WORKERS];
    pthread_t              threads[MAX_WORKERS];
    uint32_t               workerCount;
    atomic_int             running;
    atomic_uint64          nextJobId;
    Job                    jobs[MAX_JOBS];
    atomic_int             jobCount;
    pthread_mutex_t        jobPoolMutex;
};

static void *job_worker(void *arg)
{
    WorkerThreadArgs *args = (WorkerThreadArgs *)arg;
    JobSystem *sys = args->jobSystem;
    uint32_t workerIndex = args->workerIndex;
    free(args);
    
    while (atomic_load(&sys->running)) {
        Job *job = NULL;
        
        JobQueue *myQueue = &sys->queues[workerIndex];
        pthread_mutex_lock(&myQueue->mutex);
        if (myQueue->head < myQueue->tail) {
            job = myQueue->jobs[myQueue->head++ % MAX_JOB_QUEUE];
        }
        pthread_mutex_unlock(&myQueue->mutex);
        
        if (!job) {
            for (uint32_t i = 0; i < sys->workerCount; i++) {
                if (i == workerIndex) continue;
                
                JobQueue *otherQueue = &sys->queues[i];
                pthread_mutex_lock(&otherQueue->mutex);
                if (otherQueue->head < otherQueue->tail) {
                    uint32_t stealIndex = (otherQueue->tail - 1) % MAX_JOB_QUEUE;
                    job = otherQueue->jobs[stealIndex];
                    otherQueue->tail--;
                }
                pthread_mutex_unlock(&otherQueue->mutex);
                
                if (job) break;
            }
        }
        
        if (job) {
            uint32_t myJobIndex = atomic_fetch_add_32(&job->currentIndex, 1);
            if (myJobIndex < job->jobCount) {
                job->function(job->context, myJobIndex);
                atomic_fetch_add_32(&job->completedCount, 1);
            }
            if (atomic_fetch_add(&job->refCount, -1) == 1) {
                atomic_store_64(&job->handleId, 0);
                atomic_fetch_add(&sys->jobCount, -1);
            }
        } else {
            pthread_sleep_np(1);
        }
    }
    
    return NULL;
}

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
    pthread_mutex_init(&sys->jobPoolMutex, NULL);
    
    for (uint32_t i = 0; i < sys->workerCount; i++) {
        pthread_mutex_init(&sys->queues[i].mutex, NULL);
        sys->queues[i].head = 0;
        sys->queues[i].tail = 0;
    }
    
    for (uint32_t i = 0; i < sys->workerCount; i++) {
        WorkerThreadArgs *args = (WorkerThreadArgs *)malloc(sizeof(WorkerThreadArgs));
        if (!args) {
            free(sys);
            return NULL;
        }
        args->jobSystem = sys;
        args->workerIndex = i;
        
        if (pthread_create(&sys->threads[i], NULL, job_worker, (void *)args) != 0) {
            free(args);
            free(sys);
            return NULL;
        }
    }
    
    return sys;
}

void job_destroy(JobSystem *jobSystem)
{
    if (!jobSystem) return;
    
    atomic_store(&jobSystem->running, 0);
    
    for (uint32_t i = 0; i < jobSystem->workerCount; i++) {
        pthread_join(jobSystem->threads[i], NULL);
    }
    
    for (uint32_t i = 0; i < jobSystem->workerCount; i++) {
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

    Job *job = NULL;
    pthread_mutex_lock(&jobSystem->jobPoolMutex);
    for (uint32_t i = 0; i < MAX_JOBS; i++) {
        Job *candidate = &jobSystem->jobs[i];
        if (atomic_load(&candidate->refCount) == 0 && atomic_load_64(&candidate->handleId) == 0) {
            job = candidate;
            break;
        }
    }
    if (!job) {
        pthread_mutex_unlock(&jobSystem->jobPoolMutex);
        return (JobHandle){0};
    }
    
    job->function = descriptor->function;
    job->context = descriptor->context;
    job->jobCount = descriptor->jobCount;
    atomic_store(&job->completedCount, 0);
    atomic_store(&job->currentIndex, 0);
    job->name = descriptor->name;
    uint64_t handleId = atomic_fetch_add_64(&jobSystem->nextJobId, 1);
    atomic_store_64(&job->handleId, handleId);
    atomic_store(&job->refCount, (int)descriptor->jobCount);
    atomic_fetch_add(&jobSystem->jobCount, 1);
    pthread_mutex_unlock(&jobSystem->jobPoolMutex);
    
    uint32_t queueIndex = 0;
    for (uint32_t i = 0; i < descriptor->jobCount; i++) {
        JobQueue *queue = &jobSystem->queues[queueIndex % jobSystem->workerCount];
        
        for (;;) {
            pthread_mutex_lock(&queue->mutex);
            if (queue->tail - queue->head < MAX_JOB_QUEUE) {
                queue->jobs[queue->tail++ % MAX_JOB_QUEUE] = job;
                pthread_mutex_unlock(&queue->mutex);
                break;
            }
            pthread_mutex_unlock(&queue->mutex);
            pthread_sleep_np(1);
        }
        
        queueIndex++;
    }
    
    return (JobHandle){handleId};
}

void job_wait(JobSystem *jobSystem, JobHandle handle)
{
    if (!jobSystem || handle.id == 0) return;
    
    for (uint32_t i = 0; i < MAX_JOBS; i++) {
        if (atomic_load_64(&jobSystem->jobs[i].handleId) == handle.id) {
            Job *job = &jobSystem->jobs[i];
            while (atomic_load(&job->refCount) > 0) {
                pthread_sleep_np(0);
            }
            return;
        }
    }
}

void job_waitall(JobSystem *jobSystem)
{
    if (!jobSystem) return;
    
    while (atomic_load(&jobSystem->jobCount) > 0) {
        int allDone = 1;
        for (uint32_t i = 0; i < MAX_JOBS; i++) {
            if (atomic_load(&jobSystem->jobs[i].refCount) > 0) {
                allDone = 0;
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
    
    for (uint32_t i = 0; i < MAX_JOBS; i++) {
        if (atomic_load_64(&jobSystem->jobs[i].handleId) == handle.id) {
            return atomic_load(&jobSystem->jobs[i].refCount) == 0;
        }
    }
    return 1;
}

uint32_t job_getworkercount(JobSystem *jobSystem)
{
    return jobSystem ? jobSystem->workerCount : 0;
}

JobCounter job_createcounter(void)
{
    return (JobCounter){0};
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
        pthread_sleep_np(0);
    }
}
