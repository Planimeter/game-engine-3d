/* Copyright Planimeter. All Rights Reserved. */

#ifndef JOB_H
#define JOB_H

#include <stdint.h>
#include <stddef.h>

#if defined(__cplusplus)
    #include <atomic>
    typedef std::atomic<uint64_t> job_atomic_u64;
    static inline uint64_t job_atomic_load_u64(job_atomic_u64 *value) {
        return value->load(std::memory_order_acquire);
    }
    static inline uint64_t job_atomic_fetch_add_u64(job_atomic_u64 *value, uint64_t add) {
        return value->fetch_add(add, std::memory_order_acq_rel);
    }
#elif defined(_WIN32)
    #include <windows.h>
    typedef volatile LONG64 job_atomic_u64;
    static inline uint64_t job_atomic_load_u64(job_atomic_u64 *value) {
        return (uint64_t)InterlockedCompareExchange64((LONG64 *)value, 0, 0);
    }
    static inline uint64_t job_atomic_fetch_add_u64(job_atomic_u64 *value, uint64_t add) {
        return (uint64_t)InterlockedExchangeAdd64((LONG64 *)value, (LONG64)add);
    }
#else
    #include <stdatomic.h>
    typedef _Atomic(uint64_t) job_atomic_u64;
    static inline uint64_t job_atomic_load_u64(job_atomic_u64 *value) {
        return atomic_load(value);
    }
    static inline uint64_t job_atomic_fetch_add_u64(job_atomic_u64 *value, uint64_t add) {
        return atomic_fetch_add(value, add);
    }
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*JobFunction)(void *context, uint32_t jobIndex);

typedef struct JobSystem JobSystem;

typedef struct {
    uint64_t id;
} JobHandle;

typedef struct {
    JobFunction function;
    void        *context;
    uint32_t     jobCount;
    const char  *name;
} JobDescriptor;

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
