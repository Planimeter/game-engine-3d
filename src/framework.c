/* Copyright Planimeter. All Rights Reserved. */

#include "filesystem.h"
#include "window.h"
#include "graphics.h"
#include "audio.h"
#include "font.h"
#include "text.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static Font *g_testFont = NULL;
static Text *g_testText = NULL;
static uint64_t g_accumMs = 0;
static uint32_t g_frameCount = 0;
static int g_displayFps = 0;
static const uint64_t g_fpsUpdateIntervalMs = 1000;

void framework_init(const char *argv0)
{
    filesystem_init(argv0);
    window_init();
    audio_init(argv0);
    graphics_init();
}

void framework_load(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

#if defined(__APPLE__)
    /* Try SFNS first (macOS native), fall back to Geneva if SIP blocks access */
    g_testFont = font_create("/System/Library/Fonts/SFNS.ttf", 16);
    if (!g_testFont) {
        g_testFont = font_create("/System/Library/Fonts/Geneva.ttf", 16);
    }
#elif defined(_WIN32)
    g_testFont = font_create("Fonts/segoeui.ttf", 16);
#else
    g_testFont = font_create("Fonts/segoeui.ttf", 16);
#endif

    if (g_testFont) {
        g_testText = text_create(g_testFont, "FPS: 0");
    }
}

void framework_update(uint64_t deltaTime)
{
    g_accumMs += deltaTime;
    g_frameCount += 1;

    if (g_accumMs >= g_fpsUpdateIntervalMs) {
        g_displayFps = (int)((g_frameCount * 1000ULL) / g_accumMs);
        g_accumMs -= g_fpsUpdateIntervalMs;
        g_frameCount = 0;

        if (g_testText) {
            char fpsText[64];
            snprintf(fpsText, sizeof(fpsText), "FPS: %d", g_displayFps);
            text_set(g_testText, fpsText);
        }
    }
}

void framework_draw(void)
{
    if (!g_testText || !g_testFont) {
        return;
    }

    font_begin_batch(g_testFont);
    text_draw(g_testText, 8.0f, 8.0f, 0.0f,
              1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    font_end_batch(g_testFont);
}

int framework_quit(void)
{
    audio_quit();
    return 1;
}

void framework_lowmemory(void)
{
}

void framework_visible(int visible)
{
}

void framework_move(int x, int y)
{
}

void framework_resize(int width, int height)
{
}

void framework_minimize(void)
{
}

void framework_maximize(void)
{
}

void framework_restore(void)
{
}

void framework_mousefocus(int focus)
{
}

void framework_focus(int focus)
{
}

void framework_keypressed(const char *key, const char *scancode, int isrepeat)
{
}

void framework_keyreleased(const char *key, const char *scancode)
{
}

void framework_textedited(const char *text, int start, int length)
{
}

void framework_textinput(const char *text)
{
}

void framework_mousemoved(int x, int y, int dx, int dy, int istouch)
{
}

void framework_mousepressed(int x, int y, const char *button, int istouch)
{
}

void framework_mousereleased(int x, int y, const char *button, int istouch)
{
}

void framework_wheelmoved(int x, int y)
{
}

void framework_joystick_axis_motion(int which, int axis, int value) { (void)which; (void)axis; (void)value; }
void framework_joystick_ball_motion(int which, int ball, int dx, int dy) { (void)which; (void)ball; (void)dx; (void)dy; }
void framework_joystick_hat_motion(int which, int hat, int value) { (void)which; (void)hat; (void)value; }
void framework_joystick_button_pressed(int which, int button) { (void)which; (void)button; }
void framework_joystick_button_released(int which, int button) { (void)which; (void)button; }
void framework_joystick_added(int which) { (void)which; }
void framework_joystick_removed(int which) { (void)which; }
void framework_gamepad_axis_motion(int which, int axis, int value) { (void)which; (void)axis; (void)value; }
void framework_gamepad_button_pressed(int which, int button) { (void)which; (void)button; }
void framework_gamepad_button_released(int which, int button) { (void)which; (void)button; }
void framework_gamepad_added(int which) { (void)which; }
void framework_gamepad_removed(int which) { (void)which; }
void framework_gamepad_remapped(int which) { (void)which; }
