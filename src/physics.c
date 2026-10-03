/* Copyright Planimeter. All Rights Reserved. */

#include "physics.h"

#include "tilemap.h"

#include <math.h>

#define PHYSICS_EPSILON 0.001f

/* 128 tiles across, matching tilemap.c. Kept here because the character's
 * bounds clamp needs the extent and physics should not have to include
 * tilemap.h just to learn the map's size. */
#define PHYSICS_TILES_PER_SIDE 128
#define PHYSICS_TILE_SIZE 4.0f

/* Peak height of the tiled landscape. Kept in step with tilemap.c's own
 * amplitude: the two must agree or the character walks on a surface the
 * camera does not see. Lowered from the old heightfield's 9 so that the step
 * between neighbouring tiles stays walkable. */
#define PHYSICS_TERRAIN_AMPLITUDE 7.0f

float physics_tile_size(void)
{
    return PHYSICS_TILE_SIZE;
}

/* The world's ground is a stepped tile lattice, not a heightfield. Routing the
 * single height query through tilemap_height_at() means the mesh, the
 * character's feet and the camera all read the same function, so a tile step
 * cannot show up as the camera hovering or sinking into the ground.
 *
 * `amplitude` is accepted for signature compatibility with the old
 * heightfield call and deliberately unused: the tilemap owns its own
 * amplitude, and honouring this one as well would let the two disagree. */
float physics_terrain_height(float x, float z, uint32_t seed, float amplitude)
{
    (void)amplitude;

    /* The map is fixed for the process, so this is a constant. */
    TileMapConfig config;
    config.size = physics_tile_size();
    config.resolution = 1.0f;
    config.seed = seed;

    return tilemap_height_at(&config, x, z);
}

/* A tile step is a ledge the character has to be able to walk up, so the step
 * height is derived from the terrain's own amplitude rather than guessed.
 *
 * Getting this wrong is subtle and expensive: with the step taller than the
 * step height, slope rejection correctly refuses the ledge and the character
 * crawls at a fraction of walk speed. That reads as lost momentum rather than
 * as an unwalkable world, which is what sent this looking for a physics bug
 * that was not there. */
float physics_terrain_step(void)
{
    return PHYSICS_TERRAIN_AMPLITUDE * 0.35f;
}

/* Push a circle out of an axis-aligned box along the axis of least
 * penetration, which is the standard 2D resolution and reads as sliding
 * along a wall rather than stopping dead against it. Horizontal only:
 * vertical penetration is left to physics_surface_height(), which decides
 * whether a box top is ground the character may stand on. */
int physics_resolve_box(float pos[3], float velocity[3], float radius,
                       float stepHeight, const PhysicsBox *box)
{
    if (!box) {
        return 0;
    }

    float top = box->center[1] + box->half[1];
    /* Already standing at or above the top: this is a step to climb, not a
     * wall. physics_surface_height() lifts the feet onto it. */
    if (pos[1] >= top - PHYSICS_EPSILON) {
        return 0;
    }
    /* A box whose top is within step height of the feet is a kerb, not an
     * obstacle, so the walk carries over it. */
    if (top - pos[1] <= stepHeight) {
        return 0;
    }

    float dx = pos[0] - box->center[0];
    float dz = pos[2] - box->center[2];
    float overlapX = box->half[0] + radius - fabsf(dx);
    float overlapZ = box->half[2] + radius - fabsf(dz);
    if (overlapX <= 0.0f || overlapZ <= 0.0f) {
        return 0;
    }

    if (overlapX < overlapZ) {
        pos[0] += (dx < 0.0f ? -overlapX : overlapX);
        velocity[0] = 0.0f;
    } else {
        pos[2] += (dz < 0.0f ? -overlapZ : overlapZ);
        velocity[2] = 0.0f;
    }

    return 1;
}

float physics_surface_height(float x, float z, uint32_t seed, float amplitude,
                             const PhysicsBox *boxes, int boxCount)
{
    float surface = physics_terrain_height(x, z, seed, amplitude);

    for (int i = 0; boxes && i < boxCount; i++) {
        const PhysicsBox *box = &boxes[i];
        if (fabsf(x - box->center[0]) <= box->half[0] &&
            fabsf(z - box->center[2]) <= box->half[2]) {
            float top = box->center[1] + box->half[1];
            if (top > surface) {
                surface = top;
            }
        }
    }

    return surface;
}

int physics_slope_blocks(int hasGround, float dx, float dz, float rise,
                         float maxSlope, float stepHeight)
{
    if (!hasGround) {
        return 0;
    }

    float run = dx * dx + dz * dz;
    if (run < 1e-8f) {
        /* Standing still: the character is wherever it was put. */
        return 0;
    }

    /* If the ground under the destination is at or below the step height it
     * is walkable regardless of slope; this covers flat ground measured with
     * a large horizontal step. */
    if (rise <= stepHeight) {
        return 0;
    }

    return atan2f(rise, sqrtf(run)) > maxSlope;
}

void physics_clamp_length(float vec[2], float max)
{
    float len = sqrtf(vec[0] * vec[0] + vec[1] * vec[1]);
    if (len > max && len > 1e-6f) {
        float scale = max / len;
        vec[0] *= scale;
        vec[1] *= scale;
    }
}
