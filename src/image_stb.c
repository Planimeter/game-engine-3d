/* Copyright Planimeter. All Rights Reserved. */

#include "image.h"
#include "filesystem.h"
#include "graphics.h"
#include <stdlib.h>
#include <stdio.h>

/* Use stb_image vendored in third_party. */
#define STB_IMAGE_IMPLEMENTATION
#include "stb-2.30/stb_image.h"

/* Read a file, preferring the real filesystem for absolute paths. PhysFS is
 * mounted at the executable directory, so model textures referenced by
 * absolute repo path are invisible to it. */
static unsigned char *read_image_file(const char *path, size_t *outSize)
{
    void *filedata = NULL;
    size_t filesize = filesystem_fileread(&filedata, path);
    if (filesize > 0 && filedata) {
        *outSize = filesize;
        return (unsigned char *)filedata;
    }

    /* Absolute path outside the PhysFS mount: read it directly. */
    if (path && path[0] == '/') {
        FILE *f = fopen(path, "rb");
        if (!f) {
            return NULL;
        }
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len <= 0) {
            fclose(f);
            return NULL;
        }
        unsigned char *buf = (unsigned char *)malloc((size_t)len);
        if (!buf) {
            fclose(f);
            return NULL;
        }
        size_t got = fread(buf, 1, (size_t)len, f);
        fclose(f);
        if (got != (size_t)len) {
            free(buf);
            return NULL;
        }
        *outSize = (size_t)len;
        return buf;
    }

    return NULL;
}

Texture image_load(const char *path)
{
    if ( path == NULL ) {
        return NULL;
    }

    size_t filesize = 0;
    unsigned char *filedata = read_image_file(path, &filesize);
    if (filesize == 0 || filedata == NULL) {
        return NULL;
    }

    int width = 0, height = 0, channels = 0;
    unsigned char *pixels = stbi_load_from_memory(filedata,
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
