/* Copyright Planimeter. All Rights Reserved. */

#include "framework.h"
#include "event.h"
#include "timer.h"
#include "graphics.h"
#include "job.h"
#include <stdlib.h>
#include <stdio.h>

static JobSystem *g_jobSystem = NULL;

static void job_update(void *context, uint32_t jobIndex)
{
    (void)context;
    (void)jobIndex;
    framework_update(*(uint64_t *)context);
}

static void job_draw(void *context, uint32_t jobIndex)
{
    (void)context;
    (void)jobIndex;
    graphics_predraw();
    framework_draw();
    graphics_postdraw();
}

int main(int argc, char *argv[])
{
    framework_init(argv[0]);
    framework_load(argc, argv);

    g_jobSystem = job_create(0);
    if (!g_jobSystem) {
        fprintf(stderr, "Failed to create job system\n");
        return 1;
    }

    while (event_poll()) {
        uint64_t frameTime = timer_step();

        JobDescriptor updateJob = {
            .function = job_update,
            .context = &frameTime,
            .jobCount = 1,
            .name = "Update"
        };
        JobHandle updateHandle = job_submit(g_jobSystem, &updateJob);
        job_wait(g_jobSystem, updateHandle);

        JobDescriptor drawJob = {
            .function = job_draw,
            .context = NULL,
            .jobCount = 1,
            .name = "Draw"
        };
        JobHandle drawHandle = job_submit(g_jobSystem, &drawJob);
        job_wait(g_jobSystem, drawHandle);

        graphics_present();
    }

    job_destroy(g_jobSystem);
    return 0;
}
