/* Copyright Planimeter. All Rights Reserved. */

/* Footstep cadence checks. The test supplies its own audio backend and counts
 * what was played, so the module can be verified without an audio device:
 *
 *   cmake --build build --target test_footstep && ./build/test_footstep
 */

#include "footstep.h"
#include <stdio.h>
/* Stub the audio backend so cadence can be counted without a device. */
static int g_played;
static int g_released;
void *audio_load_sample(const char *p){ (void)p; return (void*)0x1; }
void audio_free_sample(void *s){ (void)s; }
int audio_play_sample(void *s, int loop){ (void)loop; if(s) g_played++; return 100 + g_played; }
void audio_stop_source(int src){ (void)src; g_released++; }
int audio_get_source_state(int src){ (void)src; return 0; }  /* one-shots end immediately */
void audio_init(const char*a){(void)a;} void audio_quit(void){}

static int fails;
static void check(int c, const char *m){ printf("  %-50s %s\n", m, c?"ok":"FAIL"); if(!c) fails++; }

int main(void){
    const char *names[] = {"a","b","c","d"};
    FootstepSystem *fs = footstep_create(names, 4, "land");

    /* 3 units of travel at walk speed = exactly one step. */
    g_played = 0;
    float dt = 1.0f/60.0f;
    int wasG = 1;
    for (int i = 0; i < 60; i++) {                 /* 1 second at 9 u/s = 9 units */
        footstep_update(fs, 9.0f, 1, wasG, dt); wasG = 1;
    }
    check(g_played == 3, "walk: 9 units is 3 steps at a 3-unit stride");

    /* Standing still must not play anything. */
    g_played = 0;
    for (int i = 0; i < 120; i++) footstep_update(fs, 0.0f, 1, 1, dt);
    check(g_played == 0, "standing still plays no steps");

    /* Sprint covers twice the ground in the same time, so more steps. */
    g_played = 0;
    for (int i = 0; i < 60; i++) footstep_update(fs, 18.0f, 1, 1, dt);
    check(g_played == 6, "sprint: 18 units is 6 steps");

    /* A brief hop is not a landing. */
    g_played = 0; wasG = 1;
    for (int i = 0; i < 5; i++) { footstep_update(fs, 9.0f, 0, wasG, dt); wasG = 0; }
    footstep_update(fs, 9.0f, 1, 0, dt);
    check(g_played == 0, "a short hop does not thud");

    /* A real fall does land. */
    g_played = 0; wasG = 1;
    for (int i = 0; i < 30; i++) { footstep_update(fs, 9.0f, 0, wasG, dt); wasG = 0; }
    footstep_update(fs, 9.0f, 1, 0, dt);
    check(g_played == 1, "a real fall plays one landing");

    /* One-shots must be reclaimed, not leaked. */
    check(g_released > 0, "finished voices are reclaimed");

    /* A long frame must not fire a burst of steps. */
    g_played = 0;
    footstep_update(fs, 9.0f, 1, 1, 0.25f);
    check(g_played <= 1, "a long frame fires at most one step");

    footstep_destroy(fs);
    printf("\n%s\n", fails ? "FAILURES PRESENT" : "all checks passed");
    return fails ? 1 : 0;
}
