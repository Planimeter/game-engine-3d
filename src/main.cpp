/* Copyright Planimeter. All Rights Reserved. */

// Example program:
// Using SDL2 to create an application window

#include "framework.h"
#include "SDL.h"
#include <stdio.h>

int main(int argc, char* argv[]) {
    framework_init(argv[0]);

    SDL_Window *window;                    // Declare a pointer

    SDL_Init(SDL_INIT_VIDEO);              // Initialize SDL2

    // Create an application window with the following settings:
    window = SDL_CreateWindow(
        "An SDL2 window",                  // window title
        SDL_WINDOWPOS_UNDEFINED,           // initial x position
        SDL_WINDOWPOS_UNDEFINED,           // initial y position
        640,                               // width, in pixels
        480,                               // height, in pixels
        SDL_WINDOW_OPENGL                  // flags - see below
    );

    // Check that the window was successfully created
    if (window == NULL) {
        // In the case that the window could not be made...
        printf("Could not create window: %s\n", SDL_GetError());
        return 1;
    }

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

    // SDL_Delay(3000);  // Pause execution for 3000 milliseconds, for example

    // Close and destroy the window
    SDL_DestroyWindow(window);

    // Clean up
    SDL_Quit();
    return 0;
}
