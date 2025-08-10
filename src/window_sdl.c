/* Copyright Planimeter. All Rights Reserved. */

#include "window.h"
#include <stdlib.h>
#include <stdio.h>
#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"

SDL_Window *window;                        /* Declare a pointer  */

void window_init()
{
    void window_shutdown(void);

    SDL_Init(SDL_INIT_VIDEO);              /* Initialize SDL3    */

    /* Create an application window with the following settings: */
    window = SDL_CreateWindow(
        "An SDL3 window",                  /* window title       */
        640,                               /* width, in pixels   */
        480,                               /* height, in pixels  */
        SDL_WINDOW_VULKAN                  /* flags - see below  */
    );

    /* Check that the window was successfully created */
    if (window == NULL) {
        /* In the case that the window could not be made... */
        printf("Could not create window: %s\n", SDL_GetError());
        return;
    }

    atexit(window_shutdown);
}

Window window_getwindow()
{
    return window;
}

void window_vulkan_createsurface(VkInstance instance, VkSurfaceKHR* surface)
{
    SDL_Vulkan_CreateSurface(window, instance, NULL, surface);
}

void window_getwindowsizeinpixels(int *w, int *h)
{
    SDL_GetWindowSizeInPixels(window, w, h);
}

void window_shutdown(void)
{
    /* Close and destroy the window */
    SDL_DestroyWindow(window);

    /* Clean up */
    SDL_Quit();
}
