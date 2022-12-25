/* Copyright Planimeter. All Rights Reserved. */

#ifndef FRAMEWORK_H
#define FRAMEWORK_H

#ifdef __cplusplus
extern "C" {
#endif

void framework_init(const char *argv0);
void framework_load(int argc, char *argv[]);
void framework_quit();
void framework_lowmemory();
void framework_visible(int visible);
void framework_move(int x, int y);
void framework_resize(int width, int height);
void framework_minimize();
void framework_maximize();
void framework_restore();
void framework_mousefocus(int focus);
void framework_focus(int focus);
void framework_keypressed(const char *key, const char *scancode, int isrepeat);
void framework_keyreleased(const char *key, const char *scancode);
void framework_textedited(const char *text, int start, int length);
void framework_textinput(const char *text);
void framework_mousemoved(int x, int y, int dx, int dy, int istouch);
void framework_mousepressed(int x, int y, const char *button, int istouch);
void framework_mousereleased(int x, int y, const char *button, int istouch);
void framework_wheelmoved(int x, int y);
void framework_update(double dt);
void framework_draw();

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_H */
