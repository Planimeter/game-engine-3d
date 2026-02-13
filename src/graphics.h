/* Copyright Planimeter. All Rights Reserved. */

#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <sys/types.h>
#include "model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *Shader;
typedef void *Texture;
typedef void *Material;
typedef void *Buffer;
typedef void *RenderPass;

typedef enum {
	BLEND_NONE,
	BLEND_ALPHA,
	BLEND_ADD,
	BLEND_PREMULT
} BlendMode;

typedef struct {
	int depthWrite;
	int depthTest;
	int backfaceCulling;
	BlendMode blendMode;
} RasterState;

void        graphics_init();
void        graphics_shutdown();
void        graphics_resize();
int         graphics_isminimized();
void        graphics_predraw();
void        graphics_postdraw();
void        graphics_present();

Shader      graphics_createshader(const char *source, size_t size,
                                  const char **defines, size_t defineCount);
void        graphics_destroyshader(Shader shader);
Material    graphics_creatematerial(Shader shader);
void        graphics_destroymaterial(Material mat);
void        graphics_material_set_texture(Material mat,
                                          const char *name,
                                          Texture tex);
void        graphics_material_set_float(Material mat,
                                        const char *name,
                                        float value);
void        graphics_material_set_vec3(Material mat,
                                       const char *name,
                                       float x, float y, float z);
void        graphics_material_set_mat4(Material mat,
                                       const float *matrix4x4);
void        graphics_setmaterial(Material mat);

Model      *graphics_loadmodel(const char *filepath);
void        graphics_destroymodel(Model *model);
void        graphics_drawmodel(Model *model,
                               Material mat,
                               const float *transform4x4);
void        graphics_draw_instanced(Model *model,
                                    Material mat,
                                    const float *transforms4x4,
                                    size_t count);

Buffer      graphics_createvertexbuffer(const void *data, size_t size);
Buffer      graphics_createindexbuffer(const void *data, size_t size);
Buffer      graphics_createuniformbuffer(size_t size);
void        graphics_updatebuffer(Buffer buf, const void *data, size_t size);
void        graphics_destroybuffer(Buffer buf);

Texture     graphics_createtexture(Texture src);
void        graphics_destroytexture(Texture tex);
void        graphics_bindtexture(Texture tex, unsigned slot);

RenderPass  graphics_createpass(const char *name, RasterState state);
void        graphics_beginpass(RenderPass pass);
void        graphics_endpass(RenderPass pass);

Shader      graphics_get_shader_variant(Shader base,
                                        const char **defines,
                                        size_t defineCount);

#ifdef __cplusplus
}
#endif

#endif /* GRAPHICS_H */
