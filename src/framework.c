/* Copyright Planimeter. All Rights Reserved. */

#include "filesystem.h"
#include "window.h"
#include "graphics.h"
#include "font.h"
#include "text.h"
#include <stdint.h>
#include <stdlib.h>

static Font *g_testFont = NULL;
static Text *g_testText = NULL;

void framework_init(const char *argv0)
{
    filesystem_init(argv0);
    window_init();
    graphics_init();
}

void framework_load(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    
    // Try to load Vera.ttf, fallback to Windows system font if missing
    g_testFont = font_create("fonts/Vera.ttf", 48);
    if (!g_testFont) {
        // Try loading Arial from Windows Fonts (correct casing)
        g_testFont = font_create("Fonts/arial.ttf", 48);
    }
    if (!g_testFont) {
        // Try loading Segoe UI as another fallback
        g_testFont = font_create("Fonts/segoeui.ttf", 48);
    }
    if (g_testFont) {
        g_testText = text_create(g_testFont, "Hello, World!");
    }
}

void framework_update(uint64_t deltaTime)
{
    (void)deltaTime;
}

void framework_draw(void)
{
    if (g_testText && g_testFont) {
        // Text coordinates are in pixels, not NDC
        // Draw near bottom-left at (50, 100)
        // (0,0) is bottom-left in pixel space for this API
        int win_w = 0, win_h = 0;
        window_getwindowsizeinpixels(&win_w, &win_h);
        float y = 100.0f + font_get_ascent(g_testFont);
        printf("DEBUG: win_h=%d, y=%.2f\n", win_h, y);
        // Draw 100 pixels from the top, baseline adjusted
        text_draw(g_testText, 50.0f, y, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    }
}

int framework_quit(void)
{
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
