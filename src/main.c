/* Copyright Planimeter. All Rights Reserved. */

#include "framework.h"
#include "event.h"
#include "timer.h"

int main(int argc, char *argv[]) {
    framework_init(argv[0]);
    framework_load(argc, argv);

    while (event_poll()) {
        uint64_t dt = timer_step();
        framework_update(dt);
        framework_draw();
        timer_sleep(1);
    }

    return 0;
}
