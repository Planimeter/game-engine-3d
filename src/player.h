/* Copyright Planimeter. All Rights Reserved. */

#ifndef PLAYER_H
#define PLAYER_H

#include <stdint.h>

#include "physics.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Character controller.
 *
 * The character is a vertical capsule approximated by its feet position and
 * a radius, which is enough for a heightfield: the only things to collide
 * with are the ground and any boxes, so the physics module answers the
 * geometry questions and this module owns the state, the feel and the
 * response.
 *
 * Vertical motion is gravity and the jump key only. There is no binding that
 * lifts the character out of the world. */

/* Shape and feel. Defaults come from player_init(); the tunables are kept in
 * the struct so a game can retune them without editing this module. */
typedef struct {
    float position[3];   /* feet, not eye level */
    float velocity[3];

    float radius;        /* horizontal collision radius */
    float height;        /* total height, for camera clamping */
    float eyeHeight;     /* eye above the feet */

    int grounded;        /* was resting on the ground at the last step */
    int hasGround;       /* 0 disables gravity and collision entirely */

    /* World parameters, so the character does not depend on globals. */
    uint32_t terrainSeed;
    float terrainAmplitude;
    float boundsExtent;  /* terrain is square, centred on the origin */

    float walkSpeed;
    float sprintSpeed;
    float groundAcceleration;
    float airAcceleration;
    float groundFriction;
    float gravity;
    float jumpSpeed;
    float maxSlope;      /* radians; steeper ground blocks movement */
    float stepHeight;    /* vertical clearance a move may climb outright */
    float terminalVelocity;
} Player;

/* One frame of input, before it is turned into forces. `forward` and `right`
 * are -1..1 strafe axes in the camera's yaw frame; pitch is ignored so
 * looking down does not slow the walk. */
typedef struct {
    float forward;
    float right;
    int jump;      /* edge-triggered by the caller */
    int sprint;
} PlayerInput;

/* Place a character standing on the terrain at (x, z), looking down -Z. */
void player_init(Player *p, float x, float z, uint32_t seed, float amplitude,
                 float extent);

/* Advance one frame. `yaw` orients the strafe axes in radians. Integrates in
 * substeps so a long frame cannot tunnel through a hill. `boxes` may be NULL
 * with `boxCount` 0, in which case only terrain collides. */
void player_update(Player *p, const PlayerInput *input, float yaw, float dt,
                   const PhysicsBox *boxes, int boxCount);

/* Eye position, i.e. feet plus eyeHeight. */
void player_eye(const Player *p, float out[3]);

/* Terrain surface height directly under the character's feet. */
float player_ground_height(const Player *p);

#ifdef __cplusplus
}
#endif

#endif /* PLAYER_H */
