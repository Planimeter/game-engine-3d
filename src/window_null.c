/* Copyright Planimeter. All Rights Reserved. */

#include "window.h"
#include <stdlib.h>

void window_init()
{
    void window_shutdown(void);

    atexit(window_shutdown);
}

Window window_getwindow()
{
    return NULL;
}

int window_vulkan_createsurface(VkInstance instance, VkSurfaceKHR* surface)
{
    (void)instance;
    (void)surface;
    return 0;
}

void window_getwindowsizeinpixels(int *w, int *h)
{
}

void window_shutdown(void)
{
}
