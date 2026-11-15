/* Copyright Planimeter. All Rights Reserved. */

#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "framework.h"
#include "event.h"
#include "timer.h"
#include "graphics.h"
#include "image.h"
#include "job.h"
#include <stdlib.h>
#include <stdio.h>

JobSystem *g_jobSystem = NULL;
static uint64_t g_frameTime = 0;
static int captureFrame = 0;

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

    g_jobSystem = job_create(0);
    if (!g_jobSystem) {
        fprintf(stderr, "Failed to create job system\n");
        return 1;
    }

    framework_load(argc, argv);
    
    while (event_poll()) {
        g_frameTime = timer_step();
        
        JobDescriptor updateJob = {
            .function = job_update,
            .context = &g_frameTime,
            .jobCount = 1,
            .name = "Update"
        };
        JobHandle updateHandle = job_submit(g_jobSystem, &updateJob);
        job_wait(g_jobSystem, updateHandle);
        
        /* The readback arms the frame it is asked about, so the draw that
         * follows it is the one whose drawable gets copied. */
        int capture = framework_capture_active() &&
                      captureFrame == framework_capture_frames() - 1;
        if (capture) {
            graphics_capture_setup();
        }

        JobDescriptor drawJob = {
            .function = job_draw,
            .context = NULL,
            .jobCount = 1,
            .name = "Draw"
        };
        JobHandle drawHandle = job_submit(g_jobSystem, &drawJob);
        job_wait(g_jobSystem, drawHandle);
        
        graphics_present();

        if (framework_capture_active()) {
            captureFrame++;
            if (capture) {
                int w = 0, h = 0;
                unsigned char *rgba = graphics_capture_read(&w, &h);
                if (rgba) {
                    if (image_write_png(framework_capture_path(), w, h, rgba)) {
                        printf("CAPTURED %s (%dx%d)\n",
                               framework_capture_path(), w, h);
                    } else {
                        fprintf(stderr, "Failed to write %s\n",
                                framework_capture_path());
                    }
                    free(rgba);
                } else {
                    fprintf(stderr, "GAME_CAPTURE: readback unavailable\n");
                }
                break;
            }
        }
    }
    
    job_destroy(g_jobSystem);
    return 0;
}