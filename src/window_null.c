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

void window_setkeyboardfocus(int focused)
{
    (void)focused;
}

void window_setmouserelative(int enabled)
{
    (void)enabled;
}

void window_getmousecapture(int *captured)
{
    if (captured) {
        *captured = 0;
    }
}

void window_getwindowsizeinpixels(int *w, int *h)
{
    (void)w;
    (void)h;
}

float window_getpixeldensity(void)
{
    return 1.0f;
}

void window_shutdown(void)
{
}
