/* Copyright Planimeter. All Rights Reserved. */

#ifndef IMAGE_H
#define IMAGE_H

#include "graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

Texture image_load(const char *path);
void    image_free(Texture tex);

/* Write an 8-bit RGBA buffer to a PNG. Used by GAME_CAPTURE so a rendered
 * frame can be diffed by image instead of by eye. */
int     image_write_png(const char *path, int width, int height,
                        const unsigned char *rgba);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_H */
