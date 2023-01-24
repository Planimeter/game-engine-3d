/* Copyright Planimeter. All Rights Reserved. */

#include <stdlib.h>

void filesystem_init(const char *argv0)
{
    void filesystem_shutdown();

    atexit(filesystem_shutdown);
}

void filesystem_shutdown()
{
}
