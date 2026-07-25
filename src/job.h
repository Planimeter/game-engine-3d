/* Copyright Planimeter. All Rights Reserved.
 *
 * Job System API
 *
 * Design inspired by industry research on game engine job systems:
 * - Molecular Matters (Stefan Reinalter): Lock-free work-stealing deques,
 *   wait-that-does-work, parent-child unfinishedJobs counters
 * - Naughty Dog (Christian Gyrling, GDC 2015): Fiber-based job system,
 *   counter-based synchronization
 * - Our Machinery (Tobias Persson): Minimal fiber-based system, 4 ring buffers
 * - Intel GTS: Micro-scheduler + macro-scheduler DAG pattern
 *
 * Handles encode slot index for O(1) job lookup. Wait functions execute
 * pending work instead of spinning. Workers use condition variables for
 * efficient sleep/wake.
 */

#ifndef JOB_H
#define JOB_H

#include <stdint.h>
#include <stddef.h>

#if defined(__cplusplus)
    #include <atomic>
    typedef std::atomic<uint32_t> job_atomic_u32;
    typedef std::atomic<uint64_t> job_atomic_u64;
    static inline uint64_t job_atomic_load_u64(job_atomic_u64 *value) {
        return value->load(std::memory_order_acquire);
    }
    static inline uint64_t job_atomic_fetch_add_u64(job_atomic_u64 *value, uint64_t add) {
        return value->fetch_add(add, std::memory_order_acq_rel);
    }
    static inline uint32_t job_atomic_load_u32(job_atomic_u32 *value) {
        return value->load(std::memory_order_acquire);
    }
    static inline uint32_t job_atomic_fetch_add_u32(job_atomic_u32 *value, uint32_t add) {
        return value->fetch_add(add, std::memory_order_acq_rel);
    }
#elif defined(_WIN32)
    #include <windows.h>
    typedef volatile LONG32 job_atomic_u32;
    typedef volatile LONG64 job_atomic_u64;
    static inline uint64_t job_atomic_load_u64(job_atomic_u64 *value) {
        return (uint64_t)InterlockedCompareExchange64((LONG64 *)value, 0, 0);
    }
    static inline uint64_t job_atomic_fetch_add_u64(job_atomic_u64 *value, uint64_t add) {
        return (uint64_t)InterlockedExchangeAdd64((LONG64 *)value, (LONG64)add);
    }
    static inline uint32_t job_atomic_load_u32(job_atomic_u32 *value) {
        return (uint32_t)InterlockedCompareExchange((LONG *)value, 0, 0);
    }
    static inline uint32_t job_atomic_fetch_add_u32(job_atomic_u32 *value, uint32_t add) {
        return (uint32_t)InterlockedExchangeAdd((LONG *)value, (LONG)add);
    }
#else
    #include <stdatomic.h>
    typedef _Atomic(uint32_t) job_atomic_u32;
    typedef _Atomic(uint64_t) job_atomic_u64;
    static inline uint64_t job_atomic_load_u64(job_atomic_u64 *value) {
        return atomic_load(value);
    }
    static inline uint64_t job_atomic_fetch_add_u64(job_atomic_u64 *value, uint64_t add) {
        return atomic_fetch_add(value, add);
    }
    static inline uint32_t job_atomic_load_u32(job_atomic_u32 *value) {
        return atomic_load(value);
    }
    static inline uint32_t job_atomic_fetch_add_u32(job_atomic_u32 *value, uint32_t add) {
        return atomic_fetch_add(value, add);
    }
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*JobFunction)(void *context, uint32_t jobIndex);

typedef struct JobSystem JobSystem;

/**
 * Opaque handle to a submitted job.
 * Encodes slot index + generation for O(1) lookup in job_wait.
 * Bits [63:48] = slot index (0..65535)
 * Bits [47:0]  = generation counter (unique per slot allocation)
 */
typedef struct {
    uint64_t id;
} JobHandle;

/**
 * Descriptor for submitting a job to the system.
 * @param function   The function to execute for each work item.
 * @param context    User-provided context pointer (passed to function).
 * @param jobCount   Number of work items (function will be called jobCount times).
 * @param name       Optional debug name for profiling.
 * @param parent     Optional parent job handle. The parent will not complete
 *                   until all children have completed (parent-child unfinishedJobs).
 *                   Pass {0} for no parent.
 */
typedef struct {
    JobFunction function;
    void        *context;
    uint32_t     jobCount;
    const char  *name;
    JobHandle    parent;  /* {0} means no parent */
} JobDescriptor;

extern JobSystem *g_jobSystem;

JobSystem  *job_get_system(void);

JobSystem  *job_create(uint32_t workerThreadCount);
void        job_destroy(JobSystem *jobSystem);
JobHandle   job_submit(JobSystem *jobSystem, const JobDescriptor *descriptor);
void        job_wait(JobSystem *jobSystem, JobHandle handle);
void        job_waitall(JobSystem *jobSystem);
int         job_isfinished(JobSystem *jobSystem, JobHandle handle);
uint32_t    job_getworkercount(JobSystem *jobSystem);

typedef struct {
    job_atomic_u64 counter;
} JobCounter;

JobCounter  job_createcounter(void);
void        job_incrementcounter(JobCounter *counter);
void        job_waitcounter(JobSystem *jobSystem, JobCounter *counter, uint64_t targetValue);

#ifdef __cplusplus
}
#endif

#endif /* JOB_H */
