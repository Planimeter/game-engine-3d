/* Copyright Planimeter. All Rights Reserved. */

#ifndef AUDIO_H
#define AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void audio_init(const char *argv0);
void audio_quit(void);

void *audio_load_sample(const char *path);
void  audio_free_sample(void *sample);
int   audio_play_sample(void *sample, int loop);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
