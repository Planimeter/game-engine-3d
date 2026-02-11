/* Copyright Planimeter. All Rights Reserved. */

#include "framework.h"
#include "window.h"
#include "SDL3/SDL.h"

static int clamp_int_from_float(float value)
{
    return (int)value;
}

static void touch_to_pixels(float norm_x, float norm_y, float norm_dx, float norm_dy,
                            int *x, int *y, int *dx, int *dy)
{
    int width = 0;
    int height = 0;

    window_getwindowsizeinpixels(&width, &height);
    *x = clamp_int_from_float(norm_x * (float)width);
    *y = clamp_int_from_float(norm_y * (float)height);
    *dx = clamp_int_from_float(norm_dx * (float)width);
    *dy = clamp_int_from_float(norm_dy * (float)height);
}

int event_poll()
{
    int game_is_still_running = 1;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {  /* poll until all events are handled! */
        /* decide what to do with this event. */
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_TERMINATING) {
            if (framework_quit()) {
                game_is_still_running = 0;
            }
        }

        switch (event.type) {
        /* Application events */
        case SDL_EVENT_LOW_MEMORY:
            framework_lowmemory();
            break;
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
        case SDL_EVENT_DID_ENTER_BACKGROUND:
        case SDL_EVENT_WILL_ENTER_FOREGROUND:
        case SDL_EVENT_DID_ENTER_FOREGROUND:

        case SDL_EVENT_LOCALE_CHANGED:

        /* Display events */
        case SDL_EVENT_DISPLAY_ORIENTATION:
        case SDL_EVENT_DISPLAY_ADDED:
        case SDL_EVENT_DISPLAY_REMOVED:
        case SDL_EVENT_DISPLAY_MOVED:
        case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
        case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
        case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:

        /* Window events */
        case SDL_EVENT_WINDOW_SHOWN:
            framework_visible(1);
            break;
        case SDL_EVENT_WINDOW_HIDDEN:
            framework_visible(0);
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
            break;
        case SDL_EVENT_WINDOW_MOVED:
            framework_move(event.window.data1, event.window.data2);
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            framework_resize(event.window.data1, event.window.data2);
            break;
        case SDL_EVENT_WINDOW_MINIMIZED:
            framework_minimize();
            break;
        case SDL_EVENT_WINDOW_MAXIMIZED:
            framework_maximize();
            break;
        case SDL_EVENT_WINDOW_RESTORED:
            framework_restore();
            break;
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            framework_mousefocus(1);
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            framework_mousefocus(0);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            framework_focus(1);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            framework_focus(0);
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (framework_quit()) {
                game_is_still_running = 0;
            }
            break;
        case SDL_EVENT_WINDOW_HIT_TEST:
        case SDL_EVENT_WINDOW_ICCPROF_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_OCCLUDED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        case SDL_EVENT_WINDOW_DESTROYED:
            break;

        /* Keyboard events */
        case SDL_EVENT_KEY_DOWN: {
            const char *key = SDL_GetKeyName(event.key.key);
            const char *scancode = SDL_GetScancodeName(event.key.scancode);
            framework_keypressed(key ? key : "", scancode ? scancode : "", event.key.repeat != 0);
            break;
        }
        case SDL_EVENT_KEY_UP: {
            const char *key = SDL_GetKeyName(event.key.key);
            const char *scancode = SDL_GetScancodeName(event.key.scancode);
            framework_keyreleased(key ? key : "", scancode ? scancode : "");
            break;
        }
        case SDL_EVENT_TEXT_EDITING:
            framework_textedited(event.edit.text, event.edit.start, event.edit.length);
            break;
        case SDL_EVENT_TEXT_INPUT:
            framework_textinput(event.text.text);
            break;
        case SDL_EVENT_KEYMAP_CHANGED:
            break;

        /* Mouse events */
        case SDL_EVENT_MOUSE_MOTION:
            framework_mousemoved(clamp_int_from_float(event.motion.x),
                                 clamp_int_from_float(event.motion.y),
                                 clamp_int_from_float(event.motion.xrel),
                                 clamp_int_from_float(event.motion.yrel),
                                 0);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            const char *button = SDL_GetMouseButtonName(event.button.button);
            framework_mousepressed(clamp_int_from_float(event.button.x),
                                   clamp_int_from_float(event.button.y),
                                   button ? button : "", 0);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const char *button = SDL_GetMouseButtonName(event.button.button);
            framework_mousereleased(clamp_int_from_float(event.button.x),
                                    clamp_int_from_float(event.button.y),
                                    button ? button : "", 0);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            framework_wheelmoved(clamp_int_from_float(event.wheel.x),
                                 clamp_int_from_float(event.wheel.y));
            break;

        /* Joystick events */
        case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        case SDL_EVENT_JOYSTICK_BALL_MOTION:
        case SDL_EVENT_JOYSTICK_HAT_MOTION:
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_UP:
        case SDL_EVENT_JOYSTICK_ADDED:
        case SDL_EVENT_JOYSTICK_REMOVED:
        case SDL_EVENT_JOYSTICK_BATTERY_UPDATED:

        /* Game controller events */
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
        case SDL_EVENT_GAMEPAD_REMAPPED:
        case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
        case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
        case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
        case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:

        /* Touch events */
        case SDL_EVENT_FINGER_DOWN: {
            int x = 0;
            int y = 0;
            int dx = 0;
            int dy = 0;
            touch_to_pixels(event.tfinger.x, event.tfinger.y, event.tfinger.dx, event.tfinger.dy,
                            &x, &y, &dx, &dy);
            framework_mousepressed(x, y, "touch", 1);
            break;
        }
        case SDL_EVENT_FINGER_UP: {
            int x = 0;
            int y = 0;
            int dx = 0;
            int dy = 0;
            touch_to_pixels(event.tfinger.x, event.tfinger.y, event.tfinger.dx, event.tfinger.dy,
                            &x, &y, &dx, &dy);
            framework_mousereleased(x, y, "touch", 1);
            break;
        }
        case SDL_EVENT_FINGER_MOTION: {
            int x = 0;
            int y = 0;
            int dx = 0;
            int dy = 0;
            touch_to_pixels(event.tfinger.x, event.tfinger.y, event.tfinger.dx, event.tfinger.dy,
                            &x, &y, &dx, &dy);
            framework_mousemoved(x, y, dx, dy, 1);
            break;
        }

        /* Clipboard events */
        case SDL_EVENT_CLIPBOARD_UPDATE:

        /* Drag and drop events */
        case SDL_EVENT_DROP_FILE:
        case SDL_EVENT_DROP_TEXT:
        case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_COMPLETE:

        /* Audio hotplug events */
        case SDL_EVENT_AUDIO_DEVICE_ADDED:
        case SDL_EVENT_AUDIO_DEVICE_REMOVED:

        /* Sensor events */
        case SDL_EVENT_SENSOR_UPDATE:

        /* Render events */
        case SDL_EVENT_RENDER_TARGETS_RESET:
        case SDL_EVENT_RENDER_DEVICE_RESET:

        /* User events */
        case SDL_EVENT_USER:

        default:
            break;
        }
    }
    return game_is_still_running;
}
