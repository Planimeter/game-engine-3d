/* Copyright Planimeter. All Rights Reserved. */

#include "player.h"

#include "physics.h"

#include <math.h>

#define PLAYER_MAX_SUBSTEP 0.0166667f /* 60 Hz, so long frames cannot tunnel */
#define PLAYER_EPSILON 0.001f

static float player_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float player_ground_height(const Player *p)
{
    if (!p || !p->hasGround) {
        return 0.0f;
    }
    return physics_terrain_height(p->position[0], p->position[2],
                                  p->terrainSeed, p->terrainAmplitude);
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
     * noise wavelength is tens of units, so a 0.45-unit radius reads as a
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
    /* Tied to the world's own tile step, with headroom: the ground is a
     * lattice of flat blocks, so every step between neighbours is a vertical
     * ledge of that height. A step height below it means the character cannot
     * walk up a single tile and crawls at a fraction of walk speed, which
     * looks like broken momentum rather than an unwalkable world. */
    p->stepHeight = physics_terrain_step();

    /* A jump has to clear the step height with room to spare, or the
     * step-down snap fires at the apex and pulls the character straight back
     * down: the jump visibly rises a little and cancels, which reads as the
     * jump not working rather than as a collision.
     *
     * The apex is jumpSpeed^2 / (2 * gravity), so requiring it to exceed the
     * step height means jumpSpeed > sqrt(2 * gravity * stepHeight). Deriving
     * the speed from the step height keeps the two from drifting apart again,
     * which is what let the apex land 0.12 units short of the step in the
     * first place. */
    {
        float needed = sqrtf(2.0f * p->gravity * p->stepHeight * 1.8f);
        if (needed > p->jumpSpeed) {
            p->jumpSpeed = needed;
        }
    }

    p->terminalVelocity = -120.0f;

    /* Drop onto the surface rather than starting buried or in the air. */
    p->position[1] = physics_terrain_height(x, z, seed, amplitude);
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
        float wish[2] = {wishX, wishZ};
        physics_clamp_length(wish, 1.0f);
        wishX = wish[0];
        wishZ = wish[1];
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

void player_update(Player *p, const PlayerInput *input, float yaw, float dt,
                   const PhysicsBox *boxes, int boxCount)
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
     * the surface clamp can resolve in one go. */
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
            float groundNow = physics_terrain_height(oldX, oldZ, p->terrainSeed,
                                                      p->terrainAmplitude);
            float groundNext = physics_terrain_height(newX, newZ, p->terrainSeed,
                                                       p->terrainAmplitude);
            float rise = groundNext - groundNow;
            if (physics_slope_blocks(1, newX - oldX, newZ - oldZ, rise,
                                     p->maxSlope, p->stepHeight)) {
                int blockedX = physics_slope_blocks(1, newX - oldX, 0.0f, rise,
                                                    p->maxSlope, p->stepHeight);
                int blockedZ = physics_slope_blocks(1, 0.0f, newZ - oldZ, rise,
                                                    p->maxSlope, p->stepHeight);
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

        /* Props. Resolve the horizontal move against every box and drop the
         * velocity component along whichever axis pushed the character, so
         * sliding along a cube is stable instead of accumulating a pushback
         * each frame. Two passes handle a corner where two boxes meet. */
        if (boxes && boxCount > 0) {
            float probe[3];
            for (int pass = 0; pass < 2; pass++) {
                probe[0] = newX;
                probe[1] = p->position[1];
                probe[2] = newZ;
                int pushed = 0;
                for (int i = 0; i < boxCount; i++) {
                    if (physics_resolve_box(probe, p->velocity, p->radius,
                                            p->stepHeight, &boxes[i])) {
                        pushed = 1;
                    }
                }
                if (!pushed) {
                    break;
                }
            }
            newX = probe[0];
            newZ = probe[2];
        }

        p->position[0] = newX;
        p->position[2] = newZ;
        p->position[1] += p->velocity[1] * step;

        if (p->hasGround) {
            float ground = physics_surface_height(newX, newZ, p->terrainSeed,
                                                  p->terrainAmplitude,
                                                  boxes, boxCount);
            if (p->position[1] <= ground) {
                p->position[1] = ground;
                if (p->velocity[1] < 0.0f) {
                    p->velocity[1] = 0.0f;
                }
                p->grounded = 1;
            } else if (p->position[1] - ground <= p->stepHeight &&
                       p->velocity[1] <= 0.0f) {
                /* Snap down onto a lower step rather than falling onto it.
                 *
                 * The ground is a lattice of flat blocks, so walking off any
                 * tile drops the character by a full step. Without this the
                 * feet leave the surface and gravity has to close a gap that
                 * is always within step height, which reads as a stumble: the
                 * character is briefly airborne on every single step and the
                 * camera bobs with it.
                 *
                 * The velocity test is what keeps a jump working. A jump rises
                 * first, and it has to clear more than a step before gravity
                 * brings it back down to this test — so a rising character is
                 * never snapped, only one that is already falling. */
                p->position[1] = ground;
                p->velocity[1] = 0.0f;
                p->grounded = 1;
            } else {
                p->grounded = 0;
            }
        }
    }
}
