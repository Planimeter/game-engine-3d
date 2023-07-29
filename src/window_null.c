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

void window_vulkan_createsurface(VkInstance instance, VkSurfaceKHR* surface)
{
}

void window_vulkan_getdrawablesize(int *w, int *h)
{
}

void window_shutdown(void)
{
}
