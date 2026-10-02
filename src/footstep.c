/* Copyright Planimeter. All Rights Reserved. */

#include "footstep.h"

#include "audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* A footfall is a one-shot, so its source has to be reclaimed once it
 * finishes or the OpenAL source pool drains after a couple of minutes of
 * walking. audio_get_source_state() reports whether a source is still
 * playing and audio_stop_source() stops and frees it, so the system keeps the
 * handles of in-flight steps and retires them on a later frame. */
#define FOOTSTEP_MAX_VOICES 8

/* Distance between footfalls. Steps are counted in distance rather than
 * seconds, so sprinting produces faster steps instead of the same cadence at
 * double volume, and slowing to a stop stops the footsteps. */
#define FOOTSTEP_STRIDE 3.0f

/* Airborne time that counts as a landing rather than a step off a kerb. */
#define FOOTSTEP_MIN_AIR_TIME 0.12f

/* Simple deterministic jitter, so the variation is not frame-order dependent
 * the way rand() would be. */
static unsigned int g_footstepNoise = 0x9e3779b9u;

static float footstep_rand(void)
{
    g_footstepNoise = g_footstepNoise * 1664525u + 1013904223u;
    return (float)((g_footstepNoise >> 8) & 0xFFFFu) / 65535.0f;
}

struct FootstepSystem {
    void *samples[FOOTSTEP_MAX_SAMPLES];
    int sampleCount;
    void *landSample;

    /* In-flight one-shots, awaiting reclamation. */
    int voices[FOOTSTEP_MAX_VOICES];
    int voiceCount;

    float distanceSinceStep;
    float airborneTime;
    int nextSample;
};

FootstepSystem *footstep_create(const char *const *stepNames, int count,
                                const char *landName)
{
    if (count > FOOTSTEP_MAX_SAMPLES) {
        count = FOOTSTEP_MAX_SAMPLES;
    }

    FootstepSystem *fs = (FootstepSystem *)calloc(1, sizeof(FootstepSystem));
    if (!fs) {
        return NULL;
    }

    fs->sampleCount = count;
    for (int i = 0; i < count; i++) {
        if (stepNames && stepNames[i]) {
            fs->samples[i] = audio_load_sample(stepNames[i]);
            if (!fs->samples[i]) {
                fprintf(stderr, "footstep: could not load %s\n", stepNames[i]);
            }
        }
    }
    if (landName) {
        fs->landSample = audio_load_sample(landName);
    }

    /* Voice slots start empty; -1 would not match the unplayed sentinel the
     * reclamation loop tests for. */
    for (int i = 0; i < FOOTSTEP_MAX_VOICES; i++) {
        fs->voices[i] = -1;
    }

    return fs;
}

void footstep_destroy(FootstepSystem *fs)
{
    if (!fs) return;

    for (int i = 0; i < fs->sampleCount; i++) {
        if (fs->samples[i]) {
            audio_free_sample(fs->samples[i]);
            fs->samples[i] = NULL;
        }
    }
    if (fs->landSample) {
        audio_free_sample(fs->landSample);
        fs->landSample = NULL;
    }

    for (int i = 0; i < FOOTSTEP_MAX_VOICES; i++) {
        if (fs->voices[i] >= 0) {
            audio_stop_source(fs->voices[i]);
            fs->voices[i] = -1;
        }
    }

    free(fs);
}

/* Start a one-shot and remember the handle so it can be reclaimed. A full
 * voice pool means the oldest is retired first; steps are short enough that
 * this is inaudible. */
static void footstep_play(FootstepSystem *fs, void *sample)
{
    if (!sample) return;

    if (fs->voiceCount >= FOOTSTEP_MAX_VOICES) {
        audio_stop_source(fs->voices[0]);
        /* Shift down so the freshest handle stays last. */
        for (int i = 1; i < FOOTSTEP_MAX_VOICES; i++) {
            fs->voices[i - 1] = fs->voices[i];
        }
        fs->voiceCount--;
    }

    int source = audio_play_sample(sample, 0);
    if (source >= 0) {
        fs->voices[fs->voiceCount++] = source;
    }
}

/* Free handles whose one-shot has finished. Done every frame, so nothing
 * depends on the audio backend reporting a completion callback. */
static void footstep_reclaim(FootstepSystem *fs)
{
    int kept = 0;
    for (int i = 0; i < fs->voiceCount; i++) {
        if (audio_get_source_state(fs->voices[i])) {
            fs->voices[kept++] = fs->voices[i];
        } else {
            audio_stop_source(fs->voices[i]);
        }
    }
    fs->voiceCount = kept;
}

void footstep_update(FootstepSystem *fs, float speed, int grounded,
                     int wasGrounded, float dt)
{
    if (!fs || fs->sampleCount == 0) {
        return;
    }

    if (dt < 0.0f) dt = 0.0f;
    footstep_reclaim(fs);

    /* Landing: the frame the character goes from airborne to grounded, but
     * only after a real fall. A step off a small lip should not thud. */
    if (grounded && !wasGrounded) {
        if (fs->airborneTime >= FOOTSTEP_MIN_AIR_TIME) {
            footstep_play(fs, fs->landSample ? fs->landSample
                                             : fs->samples[0]);
        }
        fs->airborneTime = 0.0f;
        fs->distanceSinceStep = 0.0f;
    }

    if (!grounded) {
        fs->airborneTime += dt;
        fs->distanceSinceStep = 0.0f;
        return;
    }

    if (speed <= 0.05f) {
        /* Standing still: reset the stride so the next step is a full one
         * rather than the tail of a step taken before stopping. */
        fs->distanceSinceStep = 0.0f;
        return;
    }

    fs->distanceSinceStep += speed * dt;

    /* One step per stride at most. The loop is bounded because a very long
     * frame can only add one frame's worth of distance. */
    if (fs->distanceSinceStep >= FOOTSTEP_STRIDE) {
        fs->distanceSinceStep -= FOOTSTEP_STRIDE;

        /* Rotate through the samples and offset the starting point, so
         * consecutive steps differ without repeating in a fixed order. */
        int index;
        if (footstep_rand() < 0.5f) {
            index = fs->nextSample % fs->sampleCount;
            fs->nextSample = (fs->nextSample + 1 + (int)(footstep_rand() * 2.0f))
                            % (fs->sampleCount * 2);
        } else {
            index = (fs->nextSample + 1) % fs->sampleCount;
            fs->nextSample = index;
        }

        footstep_play(fs, fs->samples[index]);
    }
}
