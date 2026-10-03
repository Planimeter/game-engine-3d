/* Copyright Planimeter. All Rights Reserved. */

#ifndef TILEMAP_H
#define TILEMAP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tiled world geometry.
 *
 * The landscape is built as a grid of flat-topped blocks on a lattice rather
 * than a smooth heightfield. Two reasons: a blocky skyline is the readable,
 * deliberately low-detail look this style wants, and a lattice has an exact
 * answer to "how high is the ground here" — it is the height of the tile the
 * point falls in — which is what makes collision and the walk camera agree
 * with what is drawn to the pixel.
 *
 * A smooth heightfield cannot do that cheaply. Sampling it for collision
 * means the ground the player stands on is a curve through the middle of the
 * triangles, not the surface itself, and every mismatch shows up as the
 * camera sinking into or hovering over the terrain. */

typedef struct {
    float size;        /* tile edge length in world units */
    float resolution;  /* subdivisions per tile edge; 1 gives a flat block */
    uint32_t seed;
} TileMapConfig;

/* Builds one merged mesh for the whole map. Returns a heap block of
 * vertices then indices in the engine's 22-float layout, or NULL. */
void *tilemap_build_mesh(const TileMapConfig *config);
void  tilemap_free_mesh(void *mesh);

/* Ground height at a world position, matching the mesh exactly. */
float tilemap_height_at(const TileMapConfig *config, float x, float z);

/* Tile index at a world position, for callers that want the tile rather than
 * just its height. */
int tilemap_index_at(const TileMapConfig *config, float x, float z,
                     int tilesPerSide);

#ifdef __cplusplus
}
#endif

#endif /* TILEMAP_H */
