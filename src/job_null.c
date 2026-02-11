/* Copyright Planimeter. All Rights Reserved. */

#include "job.h"
#include <stdlib.h>
#include <stdint.h>

static uint64_t g_jobId = 1;

struct JobSystem {
    int dummy;
};

JobSystem *job_create(uint32_t workerThreadCount)
{
    (void)workerThreadCount;
    return (JobSystem *)calloc(1, sizeof(JobSystem));
}

void job_destroy(JobSystem *jobSystem)
{
    free(jobSystem);
}

JobHandle job_submit(JobSystem *jobSystem, const JobDescriptor *descriptor)
{
    (void)jobSystem;
    
    for (uint32_t i = 0; i < descriptor->jobCount; i++) {
        descriptor->function(descriptor->context, i);
    }
    
    JobHandle handle = {g_jobId++};
    return handle;
}

void job_wait(JobSystem *jobSystem, JobHandle handle)
{
    (void)jobSystem;
    (void)handle;
}

void job_waitall(JobSystem *jobSystem)
{
    (void)jobSystem;
}

int job_isfinished(JobSystem *jobSystem, JobHandle handle)
{
    (void)jobSystem;
    (void)handle;
    return 1;
}

uint32_t job_getworkercount(JobSystem *jobSystem)
{
    (void)jobSystem;
    return 1;
}

JobCounter job_createcounter(void)
{
    return (JobCounter){0};
}

void job_incrementcounter(JobCounter *counter)
{
    if (counter) {
        counter->counter++;
    }
}

void job_waitcounter(JobSystem *jobSystem, JobCounter *counter, uint64_t targetValue)
{
    (void)jobSystem;
    (void)counter;
    (void)targetValue;
}
