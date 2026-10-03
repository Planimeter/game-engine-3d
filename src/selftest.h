/* Copyright Planimeter. All Rights Reserved. */

#ifndef SELFTEST_H
#define SELFTEST_H

#include "player.h"

/* In-game self test.
 *
 * The character controller's behaviour depends on the terrain it is standing
 * on, so asserting it from a separate harness means asserting against a
 * synthetic map and hoping the real one behaves the same. It does not: the
 * tiled world has steps, and a walk across it exercises stair climbing,
 * step-down snapping and slope refusal in a way a flat plane never does.
 *
 * So the checks run in the real window, on the real map, and report on screen.
 * The character is driven through a scripted sequence rather than simulated
 * input, so a run is deterministic and can be repeated after a change to
 * compare against the previous one.
 *
 * Enabled with GAME_SELFTEST=1. Off by default: it takes over the camera,
 * which is not something a build should do to someone who just wants to play. */

typedef struct {
    int checks;
    int failures;
    int frame;        /* frames since the run started */
    int done;
    int started;
} SelfTest;

void selftest_begin(SelfTest *t);

/* Advance one frame. Drives the character directly and fills the overlay.
 * Returns 1 while the run is still in progress. */
int  selftest_update(SelfTest *t, Player *player, const PlayerInput *input,
                     float yaw, float dt, const PhysicsBox *boxes, int boxCount);

/* Report lines for the on-screen overlay. */
const char *selftest_line(const SelfTest *t, int index);

/* Overall status: which phase is running, or the tally once finished. */
const char *selftest_status(const SelfTest *t);

/* How many lines the overlay draws: every result plus the status. Six phases
 * produce seven results, so eight lines plus the status. */
/* Overlay lines are the three shared help text objects; the rest scroll. */

#endif /* SELFTEST_H */
