/* Copyright Planimeter. All Rights Reserved. */

#include "framework.h"
#include "SDL.h"

int event_poll()
{
    int game_is_still_running = 1;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {  /* poll until all events are handled! */
        /* decide what to do with this event. */
        if (event.type == SDL_QUIT || event.type == SDL_APP_TERMINATING) {
            if (framework_quit()) {
                game_is_still_running = 0;
            }
        }

        switch (event.type) {
        /* Application events */
        case SDL_APP_LOWMEMORY:
            framework_lowmemory();
            break;
        case SDL_APP_WILLENTERBACKGROUND:
        case SDL_APP_DIDENTERBACKGROUND:
        case SDL_APP_WILLENTERFOREGROUND:
        case SDL_APP_DIDENTERFOREGROUND:

        case SDL_LOCALECHANGED:

        /* Display events */
        case SDL_DISPLAYEVENT:

        /* Window events */
        case SDL_WINDOWEVENT:
        case SDL_SYSWMEVENT:

        /* Keyboard events */
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        case SDL_TEXTEDITING:
        case SDL_TEXTINPUT:
        case SDL_KEYMAPCHANGED:
        case SDL_TEXTEDITING_EXT:

        /* Mouse events */
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:

        /* Joystick events */
        case SDL_JOYAXISMOTION:
        case SDL_JOYBALLMOTION:
        case SDL_JOYHATMOTION:
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
        case SDL_JOYDEVICEADDED:
        case SDL_JOYDEVICEREMOVED:
        case SDL_JOYBATTERYUPDATED:

        /* Game controller events */
        case SDL_CONTROLLERAXISMOTION:
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
        case SDL_CONTROLLERDEVICEREMAPPED:
        case SDL_CONTROLLERTOUCHPADDOWN:
        case SDL_CONTROLLERTOUCHPADMOTION:
        case SDL_CONTROLLERTOUCHPADUP:
        case SDL_CONTROLLERSENSORUPDATE:

        /* Touch events */
        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
        case SDL_FINGERMOTION:

        /* Gesture events */
        case SDL_DOLLARGESTURE:
        case SDL_DOLLARRECORD:
        case SDL_MULTIGESTURE:

        /* Clipboard events */
        case SDL_CLIPBOARDUPDATE:

        /* Drag and drop events */
        case SDL_DROPFILE:
        case SDL_DROPTEXT:
        case SDL_DROPBEGIN:
        case SDL_DROPCOMPLETE:

        /* Audio hotplug events */
        case SDL_AUDIODEVICEADDED:
        case SDL_AUDIODEVICEREMOVED:

        /* Sensor events */
        case SDL_SENSORUPDATE:

        /* Render events */
        case SDL_RENDER_TARGETS_RESET:
        case SDL_RENDER_DEVICE_RESET:

        /* Internal events */
        case SDL_POLLSENTINEL:

        /** Events ::SDL_USEREVENT through ::SDL_LASTEVENT are for your use,
         *  and should be allocated with SDL_RegisterEvents()
         */
        case SDL_USEREVENT:

        /**
         *  This last event is only for bounding internal arrays
         */
        case SDL_LASTEVENT:
        
        default:
            break;
        }
    }
    return game_is_still_running;
}
