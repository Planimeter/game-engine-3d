/* Copyright Planimeter. All Rights Reserved. */

#ifndef PHYSICS_H
#define PHYSICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Collision queries against a terrain heightfield and a set of boxes.
 *
 * The world is a heightfield plus axis-aligned props, so collision reduces to
 * sampling the surface height and refusing moves that climb too steeply.
 * Nothing here rotates or moves the world; callers move the character. */

/* Terrain surface height at a world position, in the same units the mesh was
 * built with. This is the stepped tile lattice, not a smooth heightfield. */
float physics_terrain_height(float x, float z, uint32_t seed, float amplitude);

/* Tile edge length of the world. The map is square and centred on the origin,
 * so its extent is tilesPerSide * this. */
float physics_tile_size(void);

/* How tall a single tile step is. The character controller's step height is
 * derived from this, so a tile ledge stays walkable; see the comment on the
 * implementation for why guessing it separately goes wrong. */
float physics_terrain_step(void);

/* An axis-aligned box the character cannot walk through. Props come from the
 * scene graph, so a cube you can see is a cube you collide with. */
typedef struct {
    float center[3];
    float half[3];
} PhysicsBox;

/* Flat circle-vs-box push-out in the XZ plane. Resolves along the axis of
 * least penetration, so a character slides along a wall rather than stopping
 * dead against it, and zeroes the velocity component that drove into the
 * face. Returns 1 if the position was moved.
 *
 * Boxes whose top is within stepHeight of the feet are treated as kerbs and
 * skipped: they are ground to climb, not walls. Pass the character's feet Y
 * in pos[1] and stepHeight 0 to treat every box as solid. */
int  physics_resolve_box(float pos[3], float velocity[3], float radius,
                         float stepHeight, const PhysicsBox *box);

/* Highest standable surface directly under a point: the terrain, or the top
 * of any box the point is inside. This is what feet rest on, so a low crate
 * reads as ground to climb rather than a wall to stop at. */
float physics_surface_height(float x, float z, uint32_t seed, float amplitude,
                             const PhysicsBox *boxes, int boxCount);

/* Whether a horizontal move from one XZ point to another is too steep to
 * walk. `rise` is destination ground minus source ground; `dx`/`dz` are the
 * horizontal run. Returns 1 to refuse the move. Rises within stepHeight are
 * walkable regardless of slope, which covers flat ground and kerbs. */
int  physics_slope_blocks(int hasGround, float dx, float dz, float rise,
                          float maxSlope, float stepHeight);

/* Clamp a vector length to at most max, in place. */
void physics_clamp_length(float vec[2], float max);

#ifdef __cplusplus
}
#endif

#endif /* PHYSICS_H */
