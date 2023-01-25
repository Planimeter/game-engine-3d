/* Copyright Planimeter. All Rights Reserved. */

#ifndef GRAPHICS_H
#define GRAPHICS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void *Shader;

void   graphics_init();
Shader graphics_createshader(const char *shader, size_t size);
void   graphics_present();
void   graphics_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* GRAPHICS_H */
