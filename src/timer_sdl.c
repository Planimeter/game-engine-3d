/* Copyright Planimeter. All Rights Reserved. */

#include "timer.h"
#include "SDL.h"

static uint64_t dt = 0;
static uint64_t prevtime = 0;

uint64_t timer_gettime()
{
    return SDL_GetTicks64();
}

uint64_t timer_step()
{
    uint64_t time = timer_gettime();
    dt = time - prevtime;
    prevtime = time;
    return dt;
}

void timer_sleep(uint32_t ms)
{
    SDL_Delay(ms);
}
