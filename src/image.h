/* Copyright Planimeter. All Rights Reserved. */

#ifndef IMAGE_H
#define IMAGE_H

#include "graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

Texture image_load(const char *path);
void    image_free(Texture tex);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_H */
