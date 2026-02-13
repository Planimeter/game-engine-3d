/* Copyright Planimeter. All Rights Reserved. */

#include "audio.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "filesystem.h"
#include <AL/al.h>
#include <AL/alc.h>

typedef struct Sample {
    ALuint buffer;
} Sample;

static ALCdevice *g_device = NULL;
static ALCcontext *g_context = NULL;

void audio_init(const char *argv0)
{
    (void)argv0;
    g_device = alcOpenDevice(NULL);
    if (!g_device) return;
    g_context = alcCreateContext(g_device, NULL);
    if (!g_context) {
        alcCloseDevice(g_device);
        g_device = NULL;
        return;
    }
    alcMakeContextCurrent(g_context);
}

void audio_quit(void)
{
    if (g_context) {
        alcMakeContextCurrent(NULL);
        alcDestroyContext(g_context);
        g_context = NULL;
    }
    if (g_device) {
        alcCloseDevice(g_device);
        g_device = NULL;
    }
}

static ALenum wav_format_from_params(int channels, int bitsPerSample)
{
    if (bitsPerSample == 8) {
        return (channels == 1) ? AL_FORMAT_MONO8 : AL_FORMAT_STEREO8;
    } else if (bitsPerSample == 16) {
        return (channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    }
    return 0;
}

void *audio_load_sample(const char *path)
{
    if (!path) return NULL;

    uint8_t *fileData = NULL;
    size_t fileSize = filesystem_fileread((void**)&fileData, path);
    if (fileSize == 0 || !fileData) return NULL;

    const uint8_t *p = fileData;
    const uint8_t *end = fileData + fileSize;

    if ((size_t)(end - p) < 12) { free(fileData); return NULL; }
    if (memcmp(p, "RIFF", 4) != 0 || memcmp(p + 8, "WAVE", 4) != 0) { free(fileData); return NULL; }
    p += 12; /* skip RIFF header */

    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
    uint8_t *data = NULL;
    uint32_t dataSize = 0;

    while (p + 8 <= end) {
        const uint8_t *chunkId = p;
        uint32_t size = 0;
        memcpy(&size, p + 4, 4);
        p += 8;
        if (p + size > end) break;

        if (memcmp(chunkId, "fmt ", 4) == 0) {
            if (size < 16) break;
            uint16_t audioFormat = 0;
            memcpy(&audioFormat, p, 2);
            memcpy(&channels, p + 2, 2);
            memcpy(&sampleRate, p + 4, 4);
            memcpy(&bitsPerSample, p + 14, 2);
            (void)audioFormat;
        } else if (memcmp(chunkId, "data", 4) == 0) {
            dataSize = (uint32_t)size;
            data = (uint8_t*)malloc(dataSize);
            if (!data) { free(fileData); return NULL; }
            memcpy(data, p, dataSize);
        }

        p += size;
        /* chunks are word aligned */
        if (size & 1) p++;
    }

    free(fileData);

    if (!data || channels == 0 || bitsPerSample == 0 || sampleRate == 0) {
        if (data) free(data);
        return NULL;
    }

    ALenum format = wav_format_from_params(channels, bitsPerSample);
    if (!format) { free(data); return NULL; }

    ALuint buffer = 0;
    alGenBuffers(1, &buffer);
    alBufferData(buffer, format, data, (ALsizei)dataSize, sampleRate);

    free(data);

    Sample *s = (Sample*)malloc(sizeof(Sample));
    if (!s) { alDeleteBuffers(1, &buffer); return NULL; }
    s->buffer = buffer;
    return s;
}

void audio_free_sample(void *sample)
{
    if (!sample) return;
    Sample *s = (Sample*)sample;
    if (s->buffer) alDeleteBuffers(1, &s->buffer);
    free(s);
}

int audio_play_sample(void *sample, int loop)
{
    if (!sample) return -1;
    Sample *s = (Sample*)sample;
    ALuint src = 0;
    alGenSources(1, &src);
    if (!src) return -1;
    alSourcei(src, AL_BUFFER, (ALint)s->buffer);
    alSourcei(src, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
    alSourcePlay(src);
    return (int)src;
}
