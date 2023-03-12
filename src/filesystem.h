/* Copyright Planimeter. All Rights Reserved. */

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void   filesystem_init(const char *argv0);
size_t filesystem_fileread(void **ptr, const char *pathname);
void   filesystem_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* FILESYSTEM_H */
