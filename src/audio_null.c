/* Copyright Planimeter. All Rights Reserved. */

#include "audio.h"
#include <stdio.h>

void audio_init(const char *argv0)
{
    (void)argv0;
}

void audio_quit(void)
{
}

void *audio_load_sample(const char *path)
{
    (void)path;
    return NULL;
}

void audio_free_sample(void *sample)
{
    (void)sample;
}

int audio_play_sample(void *sample, int loop)
{
    (void)sample;
    (void)loop;
    return -1;
}
