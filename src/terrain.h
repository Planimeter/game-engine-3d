/* Copyright Planimeter. All Rights Reserved. */

#ifndef TERRAIN_H
#define TERRAIN_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Procedural heightfield terrain.
 *
 * The mesh is a regular grid in XZ. Heights come from layered value noise,
 * so the same seed always produces the same landscape — no asset pipeline
 * needed and it can be generated at startup. */

/* Deterministic 2D value noise in [-1, 1]. Layered octaves with a smooth
 * fade; terrain_height() is this summed at several frequencies. */
float terrain_noise(float x, float z, uint32_t seed);

/* Terrain height at a world position, in the same units the mesh uses. */
float terrain_height(float x, float z, uint32_t seed);

/* Build a grid mesh centred on the origin. `resolution` is the number of
 * vertices per side (so resolution-1 quads). `extent` is the total width and
 * depth in world units; `amplitude` the peak height. Returns a heap mesh the
 * caller frees with terrain_free_mesh(), or NULL on allocation failure. */
void *terrain_build_mesh(uint32_t resolution, float extent, float amplitude,
                         uint32_t seed);
void  terrain_free_mesh(void *mesh);

#ifdef __cplusplus
}
#endif

#endif /* TERRAIN_H */
