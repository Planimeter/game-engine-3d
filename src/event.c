/* Copyright Planimeter. All Rights Reserved. */

#include "SDL.h"
#include "framework.h"

int event_poll() {
    int game_is_still_running = 1;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {  // poll until all events are handled!
        // decide what to do with this event.
        if (event.type == SDL_QUIT || event.type == SDL_APP_TERMINATING) {
            if (framework_quit()) {
                game_is_still_running = 0;
            }
        }
    }
    return game_is_still_running;
}
