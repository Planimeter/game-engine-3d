/* Copyright Planimeter. All Rights Reserved. */

#include <stdlib.h>
#include <stdio.h>

/* fsize:  return size of file "name" */
static size_t fsize(char *name, FILE *stream)
{
    return -1;
}

void filesystem_init(const char *argv0)
{
    void filesystem_shutdown(void);

    atexit(filesystem_shutdown);
}

size_t filesystem_fileread(void **ptr, const char *pathname)
{
    FILE *fp;
    size_t size;
    char *p;
    size_t elements_read;

    if ((fp = fopen(pathname, "rb")) == NULL) {
        fprintf(stderr, "filesystem_fileread: can't open %s\n", pathname);
        return 0;
    }
    size = fsize((char *)pathname, fp);
    p = (char *) malloc(size+1);  /* +1 for ′\0′ */
    if (p == NULL) {
        fclose(fp);
        return 0;
    }
    elements_read = fread(p, size, 1, fp);
    if (elements_read != 1) {
        fprintf(stderr, "filesystem_fileread: can't read %s\n", pathname);
        free(p);
        fclose(fp);
        return 0;
    }
    p[size] = '\0';
    fclose(fp);
    *ptr = p;
    return size;
}

void filesystem_shutdown(void)
{
}
