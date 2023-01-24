/* Copyright Planimeter. All Rights Reserved. */

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

void filesystem_init(const char *argv0);
char *filesystem_fileread(const char *pathname);
void filesystem_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* FILESYSTEM_H */
