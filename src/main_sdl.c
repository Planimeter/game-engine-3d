/* Copyright Planimeter. All Rights Reserved. */

#include "framework.h"
#include "event.h"
#include "timer.h"
#include "graphics.h"
#include "SDL.h"

static void load(int argc, char *argv[])
{
    framework_init(argv[0]);
    framework_load(argc, argv);
}

static void update()
{
    uint64_t dt = timer_step();
    framework_update(dt);
}

static void draw()
{
    graphics_predraw();
    framework_draw();
    graphics_postdraw();
    graphics_present();
}

int main(int argc, char *argv[])
{
    load(argc, argv);

    while (event_poll()) {
        update();
        draw();

        timer_sleep(1);
    }

    return 0;
}
