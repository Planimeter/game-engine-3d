/* Copyright Planimeter. All Rights Reserved. */

#include "physfs.h"
#include <stdlib.h>

void filesystem_init(const char *argv0)
{
    void filesystem_shutdown();

    PHYSFS_init(argv0);

    atexit(filesystem_shutdown);
}

void filesystem_shutdown()
{
    PHYSFS_deinit();
}
