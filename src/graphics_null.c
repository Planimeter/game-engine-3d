/* Copyright Planimeter. All Rights Reserved. */

#include "graphics.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NULL_MATERIAL_FLOAT_COUNT   32
#define NULL_MATERIAL_VEC3_COUNT    16
#define NULL_MATERIAL_TEXTURE_COUNT  8

typedef struct {
    float floats[NULL_MATERIAL_FLOAT_COUNT];
    size_t floatCount;
    float vec3s[NULL_MATERIAL_VEC3_COUNT * 3];
    size_t vec3Count;
    void *textures[NULL_MATERIAL_TEXTURE_COUNT];
    size_t textureCount;
    float mat4[16];
    int hasMat4;
} GPUMaterial;

typedef struct {
    void *data;
    size_t size;
} GPUBuffer;

typedef struct {
    unsigned char pixel[4];
    int width;
    int height;
} GPUTexture;

typedef struct {
    RasterState state;
} GPURenderPass;

typedef struct {
    VertexFormat format;
    RasterState state;
} GPUPipeline;

void graphics_init()
{
    void graphics_shutdown(void);

    atexit(graphics_shutdown);
}

Shader graphics_createshader(const char *shader, size_t size,
                             const char **defines, size_t defineCount)
{
    (void)defines;
    (void)defineCount;
    (void)shader;
    (void)size;
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
    (void)vertShader;
    (void)fragShader;
}

Material graphics_creatematerial(Shader shader)
{
    GPUMaterial *material = (GPUMaterial *)calloc(1, sizeof(GPUMaterial));
    (void)shader;
    return material;
}

void graphics_destroymaterial(Material mat)
{
    free(mat);
}

void graphics_material_set_texture(Material mat, const char *name, Texture tex)
{
    GPUMaterial *m = (GPUMaterial *)mat;
    if (!m || !name || !tex) return;
    if (m->textureCount < NULL_MATERIAL_TEXTURE_COUNT) {
        m->textures[m->textureCount++] = tex;
    }
}

void graphics_material_set_float(Material mat, const char *name, float value)
{
    GPUMaterial *m = (GPUMaterial *)mat;
    if (!m || !name) return;
    if (m->floatCount < NULL_MATERIAL_FLOAT_COUNT) {
        m->floats[m->floatCount++] = value;
    }
}

void graphics_material_set_vec3(Material mat, const char *name,
                                float x, float y, float z)
{
    GPUMaterial *m = (GPUMaterial *)mat;
    if (!m || !name) return;
    if (m->vec3Count < NULL_MATERIAL_VEC3_COUNT) {
        size_t idx = m->vec3Count * 3;
        m->vec3s[idx + 0] = x;
        m->vec3s[idx + 1] = y;
        m->vec3s[idx + 2] = z;
        m->vec3Count++;
    }
}

void graphics_material_set_mat4(Material mat, const float *matrix4x4)
{
    GPUMaterial *material = (GPUMaterial *)mat;
    if (!material || !matrix4x4) {
        return;
    }

    memcpy(material->mat4, matrix4x4, sizeof(material->mat4));
    material->hasMat4 = 1;
}

void graphics_setmaterial(Material mat)
{
    (void)mat;
}

Model *graphics_loadmodel(const char *filepath)
{
    return model_load(filepath);
}

void graphics_destroymodel(Model *model)
{
    model_destroy(model);
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

void graphics_draw_buffers(Buffer vertexBuffer,
                           Buffer indexBuffer,
                           size_t indexCount,
                           Material mat,
                           const float *transform4x4)
{
    (void)vertexBuffer;
    (void)indexBuffer;
    (void)indexCount;
    (void)mat;
    (void)transform4x4;
}

Buffer graphics_createvertexbuffer(const void *data, size_t size)
{
    GPUBuffer *buffer = (GPUBuffer *)calloc(1, sizeof(GPUBuffer));
    if (!buffer || size == 0) {
        free(buffer);
        return NULL;
    }

    buffer->data = malloc(size);
    if (!buffer->data) {
        free(buffer);
        return NULL;
    }

    buffer->size = size;
    if (data) {
        memcpy(buffer->data, data, size);
    }
    return buffer;
}

Buffer graphics_createindexbuffer(const void *data, size_t size)
{
    return graphics_createvertexbuffer(data, size);
}

Buffer graphics_createuniformbuffer(size_t size)
{
    return graphics_createvertexbuffer(NULL, size);
}

void graphics_updatebuffer(Buffer buf, const void *data, size_t size)
{
    GPUBuffer *buffer = (GPUBuffer *)buf;
    size_t copySize;

    if (!buffer || !buffer->data || !data) {
        return;
    }

    copySize = size < buffer->size ? size : buffer->size;
    memcpy(buffer->data, data, copySize);
}

void graphics_destroybuffer(Buffer buf)
{
    GPUBuffer *buffer = (GPUBuffer *)buf;
    if (!buffer) {
        return;
    }

    free(buffer->data);
    free(buffer);
}

void graphics_binduniformbuffer(Buffer buf, unsigned slot)
{
    (void)buf;
    (void)slot;
}

Texture graphics_createtexture(Texture src)
{
    GPUTexture *texture;

    if (src) {
        return src;
    }

    texture = (GPUTexture *)calloc(1, sizeof(GPUTexture));
    if (!texture) {
        return NULL;
    }

    texture->pixel[0] = 255;
    texture->pixel[1] = 255;
    texture->pixel[2] = 255;
    texture->pixel[3] = 255;
    texture->width = 1;
    texture->height = 1;
    return texture;
}

Texture graphics_createtexture_rgba(int width,
                                    int height,
                                    const unsigned char *pixels)
{
    GPUTexture *texture;
    size_t size;

    if (width <= 0 || height <= 0) {
        return NULL;
    }

    texture = (GPUTexture *)calloc(1, sizeof(GPUTexture));
    if (!texture) {
        return NULL;
    }

    texture->width = width;
    texture->height = height;

    size = (size_t)width * (size_t)height * 4;
    if (size >= sizeof(texture->pixel)) {
        (void)pixels;
    } else if (pixels) {
        memcpy(texture->pixel, pixels, size);
    }

    return texture;
}

void graphics_updatetexture(Texture tex,
                            int x, int y,
                            int width, int height,
                            const unsigned char *pixels)
{
    GPUTexture *texture = (GPUTexture *)tex;
    size_t size;

    if (!texture || !pixels || width <= 0 || height <= 0) {
        return;
    }
    if (x < 0 || y < 0 || x + width > texture->width || y + height > texture->height) {
        return;
    }

    size = (size_t)width * (size_t)height * 4;
    if (size <= sizeof(texture->pixel)) {
        memcpy(texture->pixel, pixels, size);
    }
}

void graphics_destroytexture(Texture tex)
{
    free(tex);
}

void graphics_bindtexture(Texture tex, unsigned slot)
{
    (void)tex;
    (void)slot;
}

RenderPass graphics_createpass(const char *name, RasterState state)
{
    GPURenderPass *pass = (GPURenderPass *)calloc(1, sizeof(GPURenderPass));
    (void)name;

    if (!pass) {
        return NULL;
    }

    pass->state = state;
    return pass;
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

Pipeline graphics_createpipeline(Shader vertShader, Shader fragShader,
                                 VertexFormat format, RasterState state)
{
    GPUPipeline *pipeline = (GPUPipeline *)calloc(1, sizeof(GPUPipeline));
    
    if (!pipeline) {
        return NULL;
    }

    pipeline->format = format;
    pipeline->state = state;
    (void)vertShader;
    (void)fragShader;
    return pipeline;
}

void graphics_bindpipeline(Pipeline pipeline)
{
    (void)pipeline;
}

void graphics_destroypipeline(Pipeline pipeline)
{
    free(pipeline);
}

void graphics_shutdown(void)
{
}
