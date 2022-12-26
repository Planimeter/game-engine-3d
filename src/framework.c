/* Copyright Planimeter. All Rights Reserved. */

#include "filesystem.h"
#include "SDL.h"
#include <stdio.h>
#include <stdlib.h>

SDL_Window *window;                        // Declare a pointer

static void atexit_SDL_DestroyWindow(void) {
    SDL_DestroyWindow(window);
}

void framework_init(const char *argv0) {
    filesystem_init(argv0);

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
        return;
    }

    // Close and destroy the window
    atexit(atexit_SDL_DestroyWindow);

    // Clean up
    atexit(SDL_Quit);
}

void framework_load(int argc, char *argv[]) {
}

int framework_quit() {
    return 1;
}

void framework_lowmemory() {
}

void framework_visible(int visible) {
}

void framework_move(int x, int y) {
}

void framework_resize(int width, int height) {
}

void framework_minimize() {
}

void framework_maximize() {
}

void framework_restore() {
}

void framework_mousefocus(int focus) {
}

void framework_focus(int focus) {
}

void framework_keypressed(const char *key, const char *scancode, int isrepeat) {
}

void framework_keyreleased(const char *key, const char *scancode) {
}

void framework_textedited(const char *text, int start, int length) {
}

void framework_textinput(const char *text) {
}

void framework_mousemoved(int x, int y, int dx, int dy, int istouch) {
}

void framework_mousepressed(int x, int y, const char *button, int istouch) {
}

void framework_mousereleased(int x, int y, const char *button, int istouch) {
}

void framework_wheelmoved(int x, int y) {
}

void framework_update(uint64_t dt) {
}

void framework_draw() {
}
