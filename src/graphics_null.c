/* Copyright Planimeter. All Rights Reserved. */

#include "graphics.h"
#include <stddef.h>
#include <stdio.h>

void graphics_init()
{
    void graphics_shutdown(void);

    atexit(graphics_shutdown);
}

Shader graphics_createshader(const char *shader, size_t size)
{
    return NULL;
}

void graphics_destroyshader(Shader shader)
{
}

int graphics_isminimized()
{
    return 0;
}

void graphics_predraw()
{
    if (graphics_isminimized())
    {
        return;
    }
}

void graphics_postdraw()
{
    if (graphics_isminimized())
    {
        return;
    }
}

void graphics_present()
{
    if (graphics_isminimized())
    {
        return;
    }
}

void graphics_resize()
{
}

void graphics_shutdown(void)
{
}
