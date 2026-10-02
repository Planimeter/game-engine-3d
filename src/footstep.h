/* Copyright Planimeter. All Rights Reserved. */

#ifndef FOOTSTEP_H
#define FOOTSTEP_H

/* Footstep audio driven by the character controller.
 *
 * Cadence comes from distance travelled, not from time, so the steps land at
 * the same rate whether the character is walking or sprinting and the sound
 * tracks the feet instead of drifting against them. The caller feeds in
 * horizontal speed; this module decides when a step has happened.
 *
 * Samples are cycled with a per-step pitch and volume variation, because four
 * identical footfalls in a row read as a loop rather than as walking. */

#define FOOTSTEP_MAX_SAMPLES 8

typedef struct FootstepSystem FootstepSystem;

/* Load `count` samples and a landing sample. Any NULL name is skipped, and a
 * missing file is not fatal: the system simply plays whatever loaded, so a
 * missing asset degrades to silence rather than to a crash. */
FootstepSystem *footstep_create(const char *const *stepNames, int count,
                                const char *landName);
void footstep_destroy(FootstepSystem *fs);

/* Called once per frame. `speed` is horizontal units per second, `grounded`
 * and `wasGrounded` are the character's grounded flag this frame and last
 * frame, and `dt` is the frame's delta in seconds. Plays a step every
 * `strideLength` units travelled while grounded and moving, and a landing
 * sound on the frame the character touches down after a real fall. */
void footstep_update(FootstepSystem *fs, float speed, int grounded,
                     int wasGrounded, float dt);

#endif /* FOOTSTEP_H */
