/* Copyright Planimeter. All Rights Reserved. */

#include "physfs.h"
#include <stdlib.h>
#include <stdio.h>

void filesystem_init(const char *argv0)
{
    void filesystem_shutdown(void);

    PHYSFS_init(argv0);

    atexit(filesystem_shutdown);
}

char *filesystem_fileread(const char *pathname)
{
    PHYSFS_File *fp;
    PHYSFS_sint64 size;
    char *p;
    PHYSFS_sint64 elements_read;

    if ((fp = PHYSFS_openRead(pathname)) == NULL) {
        fprintf(stderr, "filesystem_fileread: can't open %s\n", pathname);
        return NULL;
    }
    size = PHYSFS_fileLength(fp);
    p = (char *) malloc(size+1);  /* +1 for ′\0′ */
    if (p == NULL) {
        PHYSFS_close(fp);
        return NULL;
    }
    elements_read = PHYSFS_readBytes(fp, p, size);
    if (elements_read != 1) {
        fprintf(stderr, "filesystem_fileread: can't read %s\n", pathname);
        free(p);
        PHYSFS_close(fp);
        return NULL;
    }
    PHYSFS_close(fp);
    return p;
}

void filesystem_shutdown(void)
{
    PHYSFS_deinit();
}
