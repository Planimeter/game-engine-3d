/* Copyright Planimeter. All Rights Reserved. */

#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <sys/types.h>
#include "model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *Shader;

void   graphics_init();
Shader graphics_createshader(const char *shader, size_t size);
void   graphics_destroyshader(Shader shader);
int    graphics_isminimized();
void   graphics_predraw();
void   graphics_postdraw();
void   graphics_present();
void   graphics_resize();
void   graphics_setshader(Shader vertShader, Shader fragShader);
void   graphics_shutdown(void);

Model *graphics_loadmodel(const char *filepath);
void   graphics_destroymodel(Model *model);
void   graphics_drawmodel(Model *model);

#ifdef __cplusplus
}
#endif

#endif /* GRAPHICS_H */
