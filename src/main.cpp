/* Copyright Planimeter. All Rights Reserved. */

#include "framework.h"
#include "event.h"

int main(int argc, char *argv[]) {
    framework_init(argv[0]);
    framework_load(argc, argv);

    while (event_poll()) {
        double dt = 0.0;
        framework_update(dt);
        framework_draw();
    }

    return 0;
}
