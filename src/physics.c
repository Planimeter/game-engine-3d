/* Copyright Planimeter. All Rights Reserved. */

#include "physics.h"

#include "terrain.h"

#include <math.h>

#define PLAYER_MAX_SUBSTEP 0.0166667f /* 60 Hz, so long frames cannot tunnel */
#define PLAYER_EPSILON 0.001f

static float player_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float player_terrain_height(const Player *p, float x, float z)
{
    if (!p->hasGround) {
        return 0.0f;
    }
    return terrain_height(x, z, p->terrainSeed) * p->terrainAmplitude;
}

/* Ground height under the feet, ignoring the radius. The surface function is
 * the same one the mesh was built from, so there is no mismatch between what
 * is drawn and what is stood on. */
float player_ground_height(const Player *p)
{
    if (!p) {
        return 0.0f;
    }
    return player_terrain_height(p, p->position[0], p->position[2]);
}

void player_init(Player *p, float x, float z, uint32_t seed, float amplitude,
                 float extent)
{
    if (!p) return;

    p->position[0] = x;
    p->position[1] = 0.0f;
    p->position[2] = z;

    p->velocity[0] = 0.0f;
    p->velocity[1] = 0.0f;
    p->velocity[2] = 0.0f;

    /* Roughly human proportions against a 9-unit terrain amplitude: the
     * noise wavelength is tens of units, so a 0.9-unit radius reads as a
     * character rather than a speck. */
    p->radius = 0.45f;
    p->height = 2.0f;
    p->eyeHeight = 1.7f;

    p->grounded = 1;
    p->hasGround = 1;

    p->terrainSeed = seed;
    p->terrainAmplitude = amplitude;
    p->boundsExtent = extent;

    p->walkSpeed = 9.0f;
    p->sprintSpeed = 18.0f;
    p->groundAcceleration = 90.0f;
    p->airAcceleration = 22.0f;
    p->groundFriction = 60.0f;
    p->gravity = 26.0f;
    p->jumpSpeed = 11.0f;
    p->maxSlope = 0.9599f;   /* ~55 degrees */
    p->stepHeight = 0.7f;
    p->terminalVelocity = -120.0f;

    /* Drop onto the surface rather than starting buried or in the air. */
    p->position[1] = player_terrain_height(p, x, z);
}

void player_eye(const Player *p, float out[3])
{
    if (!p || !out) return;
    out[0] = p->position[0];
    out[1] = p->position[1] + p->eyeHeight;
    out[2] = p->position[2];
}

/* Move a horizontal vector toward a target speed with an acceleration cap.
 * Friction is applied only when there is no input, so releasing the keys
 * slides to a stop instead of stopping dead. */
static void player_accelerate(float vel[3], float wishX, float wishZ,
                              float targetSpeed, float accel, float friction,
                              float dt)
{
    float wishLen = sqrtf(wishX * wishX + wishZ * wishZ);
    float targetX = 0.0f;
    float targetZ = 0.0f;
    if (wishLen > 1e-5f) {
        if (wishLen > 1.0f) {
            wishX /= wishLen;
            wishZ /= wishLen;
        }
        targetX = wishX * targetSpeed;
        targetZ = wishZ * targetSpeed;
    }

    float dx = targetX - vel[0];
    float dz = targetZ - vel[2];
    float dLen = sqrtf(dx * dx + dz * dz);
    if (dLen < 1e-6f) {
        return;
    }

    float rate = (wishLen > 1e-5f) ? accel : friction;
    float step = rate * dt;
    if (step >= dLen) {
        vel[0] = targetX;
        vel[2] = targetZ;
    } else {
        vel[0] += dx * (step / dLen);
        vel[2] += dz * (step / dLen);
    }
}

/* Slope test in the direction of travel. Samples the ground ahead of the
 * feet and compares its rise against the horizontal run; if the resulting
 * slope is steeper than maxSlope the move is refused rather than teleporting
 * the character up a cliff. The step height is an exemption, so small
 * lips and the sampling noise between mesh and function do not block a
 * walk. */
static int player_slope_blocks(const Player *p, float x, float z,
                               float dx, float dz, float rise)
{
    if (!p->hasGround) {
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
    if (rise <= p->stepHeight) {
        return 0;
    }

    float runLen = sqrtf(run);
    float slope = atan2f(rise, runLen);
    return slope > p->maxSlope;
}

void player_update(Player *p, const PlayerInput *input, float yaw, float dt)
{
    if (!p) return;

    if (dt < 0.0f) dt = 0.0f;

    /* Yaw rotates the strafe axes into world space. The camera basis has
     * forward = (sin yaw, 0, -cos yaw) and right = (cos yaw, 0, sin yaw), so
     * the wish direction is the same combination. Pitch is deliberately not
     * used: looking at the ground should not slow the walk. */
    float sy = sinf(yaw);
    float cy = cosf(yaw);
    float wishX = 0.0f;
    float wishZ = 0.0f;
    if (input) {
        wishX = input->forward * sy + input->right * cy;
        wishZ = -input->forward * cy + input->right * sy;
    }

    float targetSpeed = (input && input->sprint) ? p->sprintSpeed : p->walkSpeed;
    if (!p->hasGround) {
        targetSpeed = 0.0f;
    }

    /* Substep so a stalled frame cannot move further than the slope test and
     * the ground clamp can resolve in one go. */
    float remaining = dt;
    int first = 1;
    while (remaining > 0.0f) {
        float step = remaining;
        if (step > PLAYER_MAX_SUBSTEP) {
            step = PLAYER_MAX_SUBSTEP;
        }
        remaining -= step;

        float accel = p->grounded ? p->groundAcceleration : p->airAcceleration;
        float friction = p->grounded ? p->groundFriction : 0.0f;
        player_accelerate(p->velocity, wishX, wishZ, targetSpeed, accel,
                          friction, step);

        /* Jump is edge-triggered by the caller, so apply it once, on the
         * first substep of the frame. */
        if (first && input && input->jump && p->grounded && p->hasGround) {
            p->velocity[1] = p->jumpSpeed;
            p->grounded = 0;
        }
        first = 0;

        if (p->hasGround) {
            p->velocity[1] -= p->gravity * step;
            if (p->velocity[1] < p->terminalVelocity) {
                p->velocity[1] = p->terminalVelocity;
            }
        } else {
            p->velocity[1] = 0.0f;
        }

        float oldX = p->position[0];
        float oldZ = p->position[2];
        float newX = oldX + p->velocity[0] * step;
        float newZ = oldZ + p->velocity[2] * step;

        /* Keep the character on the heightfield instead of walking off the
         * edge of the mesh into empty space. */
        if (p->hasGround && p->boundsExtent > 0.0f) {
            float limit = p->boundsExtent * 0.5f - p->radius - 1.0f;
            newX = player_clampf(newX, -limit, limit);
            newZ = player_clampf(newZ, -limit, limit);
        }

        /* Slope rejection. Compare the destination ground with the source
         * ground so the test is the actual rise of the walk, then slide
         * along the blocked axis instead of stopping dead, which keeps the
         * character from sticking to a wall on a diagonal approach. */
        if (p->hasGround) {
            float groundNow = player_terrain_height(p, oldX, oldZ);
            float groundNext = player_terrain_height(p, newX, newZ);
            float rise = groundNext - groundNow;
            if (player_slope_blocks(p, newX, newZ, newX - oldX, newZ - oldZ,
                                    rise)) {
                int blockedX = player_slope_blocks(p, newX, oldZ, newX - oldX,
                                                   0.0f, rise);
                int blockedZ = player_slope_blocks(p, oldX, newZ, 0.0f,
                                                   newZ - oldZ, rise);
                if (blockedX && blockedZ) {
                    newX = oldX;
                    newZ = oldZ;
                    p->velocity[0] = 0.0f;
                    p->velocity[2] = 0.0f;
                } else if (blockedX) {
                    newX = oldX;
                    p->velocity[0] = 0.0f;
                } else if (blockedZ) {
                    newZ = oldZ;
                    p->velocity[2] = 0.0f;
                }
            }
        }

        p->position[0] = newX;
        p->position[2] = newZ;
        p->position[1] += p->velocity[1] * step;

        if (p->hasGround) {
            float ground = player_terrain_height(p, newX, newZ);
            if (p->position[1] <= ground) {
                p->position[1] = ground;
                if (p->velocity[1] < 0.0f) {
                    p->velocity[1] = 0.0f;
                }
                p->grounded = 1;
            } else if (p->position[1] - ground > PLAYER_EPSILON) {
                p->grounded = 0;
            }
        }
    }
}
