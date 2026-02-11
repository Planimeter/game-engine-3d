/* Copyright Planimeter. All Rights Reserved. */

#ifndef JOB_H
#define JOB_H

#include <stdint.h>
#include <stddef.h>

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
    uint64_t counter;
} JobCounter;

JobCounter  job_createcounter(void);
void        job_incrementcounter(JobCounter *counter);
void        job_waitcounter(JobSystem *jobSystem, JobCounter *counter, uint64_t targetValue);

#ifdef __cplusplus
}
#endif

#endif /* JOB_H */
