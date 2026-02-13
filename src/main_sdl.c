/* Copyright Planimeter. All Rights Reserved. */

#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "framework.h"
#include "event.h"
#include "timer.h"
#include "graphics.h"
#include "job.h"
#include <stdlib.h>
#include <stdio.h>

JobSystem *g_jobSystem = NULL;

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
        job_submit(g_jobSystem, &updateJob);
        
        JobDescriptor drawJob = {
            .function = job_draw,
            .context = NULL,
            .jobCount = 1,
            .name = "Draw"
        };
        job_submit(g_jobSystem, &drawJob);
        
        job_waitall(g_jobSystem);
        graphics_present();
        timer_sleep(1);
    }
    
    job_destroy(g_jobSystem);
    return 0;
}
