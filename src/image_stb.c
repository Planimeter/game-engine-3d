/* Copyright Planimeter. All Rights Reserved. */

#include "image.h"
#include "filesystem.h"
#include "graphics.h"
#include <stdlib.h>
#include <stdio.h>

/* Use stb_image bundled with Assimp in third_party. */
#define STB_IMAGE_IMPLEMENTATION
#include "assimp-6.0.4/contrib/stb/stb_image.h"

Texture image_load(const char *path)
{
    if ( path == NULL ) {
        return NULL;
    }

    void *filedata = NULL;
    size_t filesize = filesystem_fileread(&filedata, path);
    if (filesize == 0 || filedata == NULL) {
        return NULL;
    }

    int width = 0, height = 0, channels = 0;
    unsigned char *pixels = stbi_load_from_memory((const unsigned char *)filedata,
                                                  (int)filesize,
                                                  &width,
                                                  &height,
                                                  &channels,
                                                  STBI_rgb_alpha);
    free(filedata);

    if (!pixels) {
        return NULL;
    }

    Texture tex = graphics_createtexture_rgba(width, height, pixels);
    stbi_image_free(pixels);
    return tex;
}

void image_free(Texture tex)
{
    if (!tex) {
        return;
    }
    graphics_destroytexture(tex);
}
