/* Copyright Planimeter. All Rights Reserved. */

#include "image.h"
#include "graphics.h"
#include <stdlib.h>

static Texture g_null_texture = NULL;

Texture image_load(const char *path)
{
    (void)path;
    if (g_null_texture) {
        return g_null_texture;
    }

    unsigned char white[4] = { 255, 255, 255, 255 };
    g_null_texture = graphics_createtexture_rgba(1, 1, white);
    return g_null_texture;
}

void image_free(Texture tex)
{
    (void)tex;
}
