/* Copyright Planimeter. All Rights Reserved. */

#include "framework.h"

int event_poll()
{
    static int game_is_still_running = 1;

    if (game_is_still_running && framework_quit()) {
        game_is_still_running = 0;
    }

    return game_is_still_running;
}
