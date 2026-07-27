/* Copyright Planimeter. All Rights Reserved. */

#include "window.h"
#include <stdlib.h>
#include <stdio.h>
#include "SDL3/SDL.h"

#ifdef __APPLE__
#include "SDL3/SDL_vulkan.h"
#include "SDL3/SDL_metal.h"
#endif

SDL_Window *window;                        /* Declare a pointer  */
static SDL_MetalView metalView = 0;        /* Metal view handle (macOS) */

void window_init()
{
    void window_shutdown(void);

    SDL_Init(SDL_INIT_VIDEO);              /* Initialize SDL3    */

#ifdef __APPLE__
    /* On macOS, we use Metal instead of Vulkan. Use SDL_WINDOW_METAL flag. */
    Uint32 windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_METAL;
#else
    /* On other platforms, request a Vulkan surface for the Vulkan backend */
    Uint32 windowFlags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
#endif

    /* Create an application window with the following settings: */
    window = SDL_CreateWindow(
        "An SDL3 window",                  /* window title       */
        640,                               /* width, in pixels   */
        480,                               /* height, in pixels  */
        windowFlags                        /* flags - see below  */
    );

    /* Check that the window was successfully created */
    if (window == NULL) {
        /* In the case that the window could not be made... */
        printf("Could not create window: %s\n", SDL_GetError());
        return;
    }

#ifdef __APPLE__
    /* Create Metal view for this window */
    metalView = SDL_Metal_CreateView(window);
    if (!metalView) {
        printf("SDL_Metal_CreateView failed: %s\n", SDL_GetError());
    }
#endif

    atexit(window_shutdown);
}

Window window_getwindow()
{
    return window;
}

void* window_get_sdl(void)
{
    return (void*)window;
}

SDL_MetalView window_get_metal_view(void)
{
#ifdef __APPLE__
    return metalView;
#else
    return 0;
#endif
}

int window_vulkan_createsurface(VkInstance instance, VkSurfaceKHR* surface)
{
#ifdef __APPLE__
    /* Metal backend doesn't use Vulkan surfaces */
    (void)instance;
    (void)surface;
    return 0;
#else
    if (!SDL_Vulkan_CreateSurface(window, instance, NULL, surface)) {
        printf("SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return 0;
    }
    return 1;
#endif
}

void window_getwindowsizeinpixels(int *w, int *h)
{
    SDL_GetWindowSizeInPixels(window, w, h);
}

void window_shutdown(void)
{
#ifdef __APPLE__
    if (metalView) {
        SDL_Metal_DestroyView(metalView);
        metalView = 0;
    }
#endif
    /* Close and destroy the window */
    SDL_DestroyWindow(window);

    /* Clean up */
    SDL_Quit();
}
