/* Copyright Planimeter. All Rights Reserved. */

#ifndef FRAMEWORK_H
#define FRAMEWORK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void framework_init(const char *argv0);
void framework_load(int argc, char *argv[]);
void framework_update(uint64_t dt);
void framework_draw(void);
int  framework_quit(void);

void framework_lowmemory(void);
void framework_visible(int visible);
void framework_move(int x, int y);
void framework_resize(int width, int height);
void framework_minimize(void);
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
void framework_joystick_axis_motion(int which, int axis, int value);
void framework_joystick_ball_motion(int which, int ball, int dx, int dy);
void framework_joystick_hat_motion(int which, int hat, int value);
void framework_joystick_button_pressed(int which, int button);
void framework_joystick_button_released(int which, int button);
void framework_joystick_added(int which);
void framework_joystick_removed(int which);
void framework_gamepad_axis_motion(int which, int axis, int value);
void framework_gamepad_button_pressed(int which, int button);
void framework_gamepad_button_released(int which, int button);
void framework_gamepad_added(int which);
void framework_gamepad_removed(int which);
void framework_gamepad_remapped(int which);

#ifdef __cplusplus
}
#endif

#endif /* FRAMEWORK_H */
