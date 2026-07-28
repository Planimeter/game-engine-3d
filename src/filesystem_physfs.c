/* Copyright Planimeter. All Rights Reserved. */

#include "physfs.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

void filesystem_init(const char *argv0)
{
    void filesystem_shutdown(void);

    PHYSFS_init(argv0);
    PHYSFS_permitSymbolicLinks(1);  /* Allow symlinks (e.g. macOS app bundle structure) */

    setvbuf(stdout, NULL, _IONBF, 0);  /* Unbuffer stdout for debug prints */

#ifdef __APPLE__
    /* Resolve the executable's directory and mount it as the root */
    char exePath[4096];
    uint32_t exePathSize = sizeof(exePath);
    if (_NSGetExecutablePath(exePath, &exePathSize) == 0) {
        /* exePath points to Contents/MacOS/game — go up to Contents/MacOS */
        char *lastSlash = strrchr(exePath, '/');
        if (lastSlash) {
            *lastSlash = '\0';  /* .../Contents/MacOS */
            printf("DEBUG: Mounting PHYSFS at '%s'\n", exePath);
            int mountResult = PHYSFS_mount(exePath, "/", 1);
            printf("DEBUG: PHYSFS_mount result: %d\n", mountResult);
            printf("DEBUG: PHYSFS_getLastError: '%s'\n", PHYSFS_getLastError());
            if (mountResult) {
                printf("DEBUG: Mount point for '.': '%s'\n", PHYSFS_getMountPoint("."));
                char **dirList = PHYSFS_enumerateFiles("");
                if (dirList) {
                    printf("DEBUG: Files in root:");
                    for (int i = 0; dirList[i]; i++) {
                        printf(" %s", dirList[i]);
                    }
                    printf("\n");
                    PHYSFS_freeList(dirList);
                }
                char **shaderFiles = PHYSFS_enumerateFiles("shaders");
                if (shaderFiles) {
                    printf("DEBUG: Files in shaders/:");
                    for (int i = 0; shaderFiles[i]; i++) {
                        printf(" %s", shaderFiles[i]);
                    }
                    printf("\n");
                    PHYSFS_freeList(shaderFiles);
                } else {
                    printf("DEBUG: No files in shaders/\n");
                }
            }
        }
    }
    /* Fallback: try CWD if executable resolution failed */
    if (!PHYSFS_getMountPoint(".")) {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd))) {
            PHYSFS_mount(cwd, "/", 1);
        }
    }
    /* Mount system fonts for macOS */
    PHYSFS_mount("/System/Library/Fonts", "Fonts", 0);
#else
    PHYSFS_mount(".", NULL, 1);
#endif

#ifdef _WIN32
    PHYSFS_mount("C:/Windows/Fonts", "Fonts", 0);
#endif

    atexit(filesystem_shutdown);
}

size_t filesystem_fileread(void **ptr, const char *pathname)
{
    PHYSFS_File *fp;
    PHYSFS_sint64 size;
    char *p;
    PHYSFS_sint64 elements_read;

    if ((fp = PHYSFS_openRead(pathname)) == NULL) {
        fprintf(stderr, "filesystem_fileread: can't open %s\n", pathname);
        return 0;
    }
    size = PHYSFS_fileLength(fp);
    p = (char *) malloc(size+1);  /* +1 for ′\0′ */
    if (p == NULL) {
        PHYSFS_close(fp);
        return 0;
    }
    elements_read = PHYSFS_readBytes(fp, p, size);
    if (elements_read != size) {
        fprintf(stderr, "filesystem_fileread: can't read %s\n", pathname);
        free(p);
        PHYSFS_close(fp);
        return 0;
    }
    p[size] = '\0';
    PHYSFS_close(fp);
    *ptr = p;
    return size;
}

void filesystem_shutdown(void)
{
    PHYSFS_deinit();
}
