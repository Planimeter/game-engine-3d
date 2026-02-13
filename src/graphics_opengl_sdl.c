/* Copyright Planimeter. All Rights Reserved. */

#include "graphics.h"
#include "window.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "SDL3/SDL.h"

SDL_GLContext context;

void graphics_init()
{
    void graphics_shutdown(void);

    Window window = window_getwindow();
    context = SDL_GL_CreateContext(window);
    atexit(graphics_shutdown);
}

Shader graphics_createshader(const char *shader, size_t size,
                             const char **defines, size_t defineCount)
{
    (void)defines;
    (void)defineCount;
    return NULL;
}

void graphics_destroyshader(Shader shader)
{
}

int graphics_isminimized()
{
    return 0;
}

void graphics_predraw()
{
    if (graphics_isminimized())
    {
        return;
    }
}

void graphics_postdraw()
{
    if (graphics_isminimized())
    {
        return;
    }
}

void graphics_present()
{
    if (graphics_isminimized())
    {
        return;
    }
}

void graphics_resize()
{
}

void graphics_setshader(Shader vertShader, Shader fragShader)
{
}

Material graphics_creatematerial(Shader shader)
{
    (void)shader;
    return NULL;
}

void graphics_destroymaterial(Material mat)
{
    (void)mat;
}

void graphics_material_set_texture(Material mat, const char *name, Texture tex)
{
    (void)mat;
    (void)name;
    (void)tex;
}

void graphics_material_set_float(Material mat, const char *name, float value)
{
    (void)mat;
    (void)name;
    (void)value;
}

void graphics_material_set_vec3(Material mat, const char *name,
                                float x, float y, float z)
{
    (void)mat;
    (void)name;
    (void)x;
    (void)y;
    (void)z;
}

void graphics_material_set_mat4(Material mat, const float *matrix4x4)
{
    (void)mat;
    (void)matrix4x4;
}

void graphics_setmaterial(Material mat)
{
    (void)mat;
}

Model *graphics_loadmodel(const char *filepath)
{
    (void)filepath;
    return NULL;
}

void graphics_destroymodel(Model *model)
{
    (void)model;
}

void graphics_drawmodel(Model *model, Material mat, const float *transform4x4)
{
    (void)model;
    (void)mat;
    (void)transform4x4;
}

void graphics_draw_instanced(Model *model,
                             Material mat,
                             const float *transforms4x4,
                             size_t count)
{
    (void)model;
    (void)mat;
    (void)transforms4x4;
    (void)count;
}

Buffer graphics_createvertexbuffer(const void *data, size_t size)
{
    (void)data;
    (void)size;
    return NULL;
}

Buffer graphics_createindexbuffer(const void *data, size_t size)
{
    (void)data;
    (void)size;
    return NULL;
}

Buffer graphics_createuniformbuffer(size_t size)
{
    (void)size;
    return NULL;
}

void graphics_updatebuffer(Buffer buf, const void *data, size_t size)
{
    (void)buf;
    (void)data;
    (void)size;
}

void graphics_destroybuffer(Buffer buf)
{
    (void)buf;
}

Texture graphics_createtexture(Texture src)
{
    (void)src;
    return NULL;
}

void graphics_destroytexture(Texture tex)
{
    (void)tex;
}

void graphics_bindtexture(Texture tex, unsigned slot)
{
    (void)tex;
    (void)slot;
}

RenderPass graphics_createpass(const char *name, RasterState state)
{
    (void)name;
    (void)state;
    return NULL;
}

void graphics_beginpass(RenderPass pass)
{
    (void)pass;
}

void graphics_endpass(RenderPass pass)
{
    (void)pass;
}

Shader graphics_get_shader_variant(Shader base,
                                   const char **defines,
                                   size_t defineCount)
{
    (void)defines;
    (void)defineCount;
    return base;
}

void graphics_shutdown(void)
{
}
