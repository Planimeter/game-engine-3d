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

void graphics_present()
{
}

void graphics_shutdown(void)
{
}
