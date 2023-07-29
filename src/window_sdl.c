/* Copyright Planimeter. All Rights Reserved. */

#include "SDL.h"
#include "SDL_vulkan.h"
#include "window.h"
#include <stdlib.h>
#include <stdio.h>

SDL_Window *window;                        /* Declare a pointer  */

void window_init()
{
    void window_shutdown(void);

    SDL_Init(SDL_INIT_VIDEO);              /* Initialize SDL2    */

    /* Create an application window with the following settings: */
    window = SDL_CreateWindow(
        "An SDL2 window",                  /* window title       */
        SDL_WINDOWPOS_UNDEFINED,           /* initial x position */
        SDL_WINDOWPOS_UNDEFINED,           /* initial y position */
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
    SDL_Vulkan_CreateSurface(window, instance, surface);
}

void window_vulkan_getdrawablesize(int *w, int *h)
{
    SDL_Vulkan_GetDrawableSize(window, w, h);
}

void window_shutdown(void)
{
    /* Close and destroy the window */
    SDL_DestroyWindow(window);

    /* Clean up */
    SDL_Quit();
}
