/* Copyright Planimeter. All Rights Reserved. */

#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

/* fsize:  return size of file "name" */
static off_t fsize(char *name, FILE *stream)
{
    struct stat stbuf;

    if (fstat(fileno(stream), &stbuf) == -1) {
        fprintf(stderr, "fsize: can't access %s\n", name);
        return -1;
    }
    return stbuf.st_size;
}

void filesystem_init(const char *argv0)
{
    void filesystem_shutdown(void);

    atexit(filesystem_shutdown);
}

char *filesystem_fileread(const char *pathname)
{
    FILE *fp;
    off_t size;
    char *p;
    size_t elements_read;

    if ((fp = fopen(pathname, "rb")) == NULL) {
        fprintf(stderr, "filesystem_fileread: can't open %s\n", pathname);
        return NULL;
    }
    size = fsize((char *)pathname, fp);
    p = (char *) malloc(size+1);  /* +1 for ′\0′ */
    if (p == NULL) {
        fclose(fp);
        return NULL;
    }
    elements_read = fread(p, size, 1, fp);
    if (elements_read != 1) {
        fprintf(stderr, "filesystem_fileread: can't read %s\n", pathname);
        free(p);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    return p;
}

void filesystem_shutdown(void)
{
}
