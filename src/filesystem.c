/* Copyright Planimeter. All Rights Reserved. */

#include "physfs.h"
#include "filesystem.h"

void filesystem_init(const char *argv0)
{
    PHYSFS_init(argv0);

    atexit(filesystem_shutdown);
}

void filesystem_shutdown()
{
    PHYSFS_deinit();
}
