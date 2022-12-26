/* Copyright Planimeter. All Rights Reserved. */

#include "SDL.h"

void timer_sleep(double seconds) { SDL_Delay((Uint32)(seconds * 1000)); }
