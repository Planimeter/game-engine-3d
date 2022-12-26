/* Copyright Planimeter. All Rights Reserved. */

// Example program:
// Using SDL2 to create an application window

#include "framework.h"
#include "SDL.h"

int main(int argc, char* argv[]) {
    framework_init(argv[0]);
    framework_load(argc, argv);

    // The window is open: could enter program loop here (see SDL_PollEvent())
    bool game_is_still_running = true;
    while (game_is_still_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {  // poll until all events are handled!
            // decide what to do with this event.
            if (event.type == SDL_QUIT || event.type == SDL_APP_TERMINATING) {
                game_is_still_running = false;
            }
        }

        // update game state, draw the current frame
    }

    return 0;
}
