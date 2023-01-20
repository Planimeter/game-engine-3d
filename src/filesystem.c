/* Copyright Planimeter. All Rights Reserved. */

#include "physfs.h"

void filesystem_init(const char *argv0)
{
    PHYSFS_init(argv0);
}
