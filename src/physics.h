/* Copyright Planimeter. All Rights Reserved. */

#ifndef PHYSICS_H
#define PHYSICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Character controller against a terrain heightfield.
 *
 * The character is a vertical capsule approximated by its feet position and
 * a radius, which is enough for a heightfield: the only thing to collide with
 * is the ground, so collision reduces to resolving the feet against the
 * sampled surface height and refusing moves that climb a slope too steep to
 * walk up.
 *
 * Nothing here rotates the world. Gravity, jump and slope response all move
 * the character; the world stays where it is. */

/* Shape and feel. Defaults come from player_init(); the tunables are kept in
 * the struct so a game can retune them without editing this module. */
typedef struct {
    float position[3];   /* feet, not eye level */
    float velocity[3];

    float radius;        /* horizontal collision radius */
    float height;        /* total height, for camera clamping */
    float eyeHeight;     /* eye above the feet */

    int grounded;        /* was resting on the terrain at the last step */
    int hasGround;       /* 0 disables gravity and collision entirely */

    /* Terrain sampling parameters, so physics does not need globals. */
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
 * substeps so a long frame cannot tunnel through a hill. */
void player_update(Player *p, const PlayerInput *input, float yaw, float dt);

/* Eye position, i.e. feet plus eyeHeight. */
void player_eye(const Player *p, float out[3]);

/* Terrain surface height directly under the character's feet. */
float player_ground_height(const Player *p);

#ifdef __cplusplus
}
#endif

#endif /* PHYSICS_H */
