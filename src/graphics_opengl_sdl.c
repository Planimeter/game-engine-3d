/* Copyright Planimeter. All Rights Reserved. */

#include "graphics.h"
#include "window.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "SDL3/SDL.h"
#include "SDL3/SDL_opengl.h"
#include "SDL3/SDL_opengl_glext.h"

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#define GL_MATERIAL_FLOAT_COUNT   32
#define GL_MATERIAL_VEC3_COUNT    16
#define GL_MATERIAL_TEXTURE_COUNT  8
#define GL_MATERIAL_BUFFER_SIZE  1024
#define GL_UNIFORM_BUFFER_SIZE   4096
#define GL_MAX_COLOR_ATTACHMENTS   4

static const float CLEAR_COLOR[4] = {0.01f, 0.01f, 0.033f, 1.0f};

/* ------------------------------------------------------------------ */
/*  OpenGL function pointers (core 3.3+)                               */
/* ------------------------------------------------------------------ */

#define GL_FUNC(ret, name, params) static ret (*name) params
GL_FUNC(void,            glActiveTexture,           (GLenum texture));
GL_FUNC(void,            glAttachShader,            (GLuint program, GLuint shader));
GL_FUNC(void,            glBindAttribLocation,      (GLuint program, GLuint index, const char *name));
GL_FUNC(void,            glBindBuffer,              (GLenum target, GLuint buffer));
GL_FUNC(void,            glBindBufferBase,          (GLenum target, GLuint index, GLuint buffer));
GL_FUNC(void,            glBindFramebuffer,         (GLenum target, GLuint framebuffer));
GL_FUNC(void,            glBindTexture,             (GLenum target, GLuint texture));
GL_FUNC(void,            glBindVertexArray,         (GLuint array));
GL_FUNC(void,            glBlendEquationSeparate,   (GLenum modeRGB, GLenum modeAlpha));
GL_FUNC(void,            glBlendFuncSeparate,       (GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha));
GL_FUNC(void,            glBufferData,              (GLenum target, GLsizeiptr size, const void *data, GLenum usage));
GL_FUNC(void,            glBufferSubData,           (GLenum target, GLintptr offset, GLsizeiptr size, const void *data));
GL_FUNC(GLenum,          glCheckFramebufferStatus,  (GLenum target));
GL_FUNC(void,            glClear,                   (GLbitfield mask));
GL_FUNC(void,            glClearColor,              (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha));
GL_FUNC(void,            glClearDepthf,             (GLfloat d));
GL_FUNC(void,            glCompileShader,           (GLuint shader));
GL_FUNC(GLuint,          glCreateProgram,           (void));
GL_FUNC(GLuint,          glCreateShader,            (GLenum type));
GL_FUNC(void,            glCullFace,                (GLenum mode));
GL_FUNC(void,            glDeleteBuffers,           (GLsizei n, const GLuint *buffers));
GL_FUNC(void,            glDeleteFramebuffers,      (GLsizei n, const GLuint *framebuffers));
GL_FUNC(void,            glDeleteProgram,           (GLuint program));
GL_FUNC(void,            glDeleteShader,            (GLuint shader));
GL_FUNC(void,            glDeleteTextures,          (GLsizei n, const GLuint *textures));
GL_FUNC(void,            glDeleteVertexArrays,      (GLsizei n, const GLuint *arrays));
GL_FUNC(void,            glDepthFunc,               (GLenum func));
GL_FUNC(void,            glDepthMask,               (GLboolean flag));
GL_FUNC(void,            glDisable,                 (GLenum cap));
GL_FUNC(void,            glDisableVertexAttribArray,(GLuint index));
GL_FUNC(void,            glDrawArrays,              (GLenum mode, GLint first, GLsizei count));
GL_FUNC(void,            glDrawElements,            (GLenum mode, GLsizei count, GLenum type, const void *indices));
GL_FUNC(void,            glDrawElementsInstanced,   (GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instanceCount));
GL_FUNC(void,            glEnable,                  (GLenum cap));
GL_FUNC(void,            glEnableVertexAttribArray, (GLuint index));
GL_FUNC(void,            glFramebufferTexture2D,    (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level));
GL_FUNC(void,            glGenBuffers,              (GLsizei n, GLuint *buffers));
GL_FUNC(void,            glGenFramebuffers,         (GLsizei n, GLuint *framebuffers));
GL_FUNC(void,            glGenTextures,             (GLsizei n, GLuint *textures));
GL_FUNC(void,            glGenVertexArrays,         (GLsizei n, GLuint *arrays));
GL_FUNC(GLint,           glGetAttribLocation,       (GLuint program, const char *name));
GL_FUNC(GLenum,          glGetError,                (void));
GL_FUNC(void,            glGetProgramInfoLog,       (GLuint program, GLsizei bufSize, GLsizei *length, char *infoLog));
GL_FUNC(void,            glGetProgramiv,            (GLuint program, GLenum pname, GLint *params));
GL_FUNC(void,            glGetShaderInfoLog,        (GLuint shader, GLsizei bufSize, GLsizei *length, char *infoLog));
GL_FUNC(void,            glGetShaderiv,             (GLuint shader, GLenum pname, GLint *params));
GL_FUNC(GLint,           glGetUniformLocation,      (GLuint program, const char *name));
GL_FUNC(void,            glLinkProgram,             (GLuint program));
GL_FUNC(void,            glPixelStorei,             (GLenum pname, GLint param));
GL_FUNC(void,            glShaderSource,            (GLuint shader, GLsizei count, const char **string, const GLint *length));
GL_FUNC(void,            glTexImage2D,              (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels));
GL_FUNC(void,            glTexParameteri,           (GLenum target, GLenum pname, GLint param));
GL_FUNC(void,            glTexSubImage2D,           (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels));
GL_FUNC(void,            glUniform1f,               (GLint location, GLfloat v0));
GL_FUNC(void,            glUniform1fv,              (GLint location, GLsizei count, const GLfloat *value));
GL_FUNC(void,            glUniform1i,               (GLint location, GLint v0));
GL_FUNC(void,            glUniform3fv,              (GLint location, GLsizei count, const GLfloat *value));
GL_FUNC(void,            glUniform4fv,              (GLint location, GLsizei count, const GLfloat *value));
GL_FUNC(void,            glUniformMatrix4fv,        (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value));
GL_FUNC(void,            glUseProgram,              (GLuint program));
GL_FUNC(void,            glVertexAttribDivisor,     (GLuint index, GLuint divisor));
GL_FUNC(void,            glVertexAttribPointer,     (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer));
GL_FUNC(void,            glViewport,                (GLint x, GLint y, GLsizei width, GLsizei height));
#undef GL_FUNC

/* ------------------------------------------------------------------ */
/*  Types                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    GLuint vertShader;
    GLuint fragShader;
} GLShader;

typedef struct {
    GLuint vao;
    GLuint vbo;
    GLuint ebo;
    size_t vertexCapacity;
    size_t indexCapacity;
    size_t vertexSize;
    size_t indexSize;
} GLBuffer;

typedef struct {
    GLuint texture;
    int width;
    int height;
} GLTexture;

typedef struct {
    Shader shader;
    GLuint uniformBuffer;
    float floats[GL_MATERIAL_FLOAT_COUNT];
    size_t floatCount;
    float vec3s[GL_MATERIAL_VEC3_COUNT * 3];
    size_t vec3Count;
    GLTexture *textures[GL_MATERIAL_TEXTURE_COUNT];
    size_t textureCount;
    float mat4[16];
    int hasMat4;
    int dirty;
} GLMaterial;

typedef struct {
    RasterState state;
    GLTexture *colorTextures[GL_MAX_COLOR_ATTACHMENTS];
    int colorCount;
    GLTexture *depthTexture;
    int hasDepth;
    GLuint fbo;
    int initialized;
} GLRenderPass;

typedef struct {
    GLuint program;
    VertexFormat format;
    RasterState state;
    GLint loc_model;
    GLint loc_view;
    GLint loc_projection;
    GLint loc_normalMatrix;
    GLint loc_tex;
    GLint loc_color;
} GLPipeline;

/* ------------------------------------------------------------------ */
/*  Global state                                                       */
/* ------------------------------------------------------------------ */

static SDL_GLContext g_context = NULL;
static SDL_Window *g_window = NULL;

static int g_windowWidth = 640;
static int g_windowHeight = 480;
static int g_minimized = 0;

static GLuint g_currentProgram = 0;
static GLuint g_currentFBO = 0;
static int g_inPass = 0;

static GLShader *g_defaultVertShader = NULL;
static GLShader *g_defaultFragShader = NULL;
static GLShader *g_textVertShader = NULL;
static GLShader *g_textFragShader = NULL;
static GLPipeline *g_defaultPipeline = NULL;
static GLPipeline *g_textPipeline = NULL;

static GLuint g_uniformBuffer = 0;

/* ------------------------------------------------------------------ */
/*  Helper: load GL function pointers                                  */
/* ------------------------------------------------------------------ */

static int gl_load_functions(void) {
#define GL_LOAD(name) \
    do { \
        name = (void (*)())SDL_GL_GetProcAddress(#name); \
        if (!name) { \
            fprintf(stderr, "OpenGL: failed to load " #name "\n"); \
            return 0; \
        } \
    } while(0)

    GL_LOAD(glActiveTexture);
    GL_LOAD(glAttachShader);
    GL_LOAD(glBindAttribLocation);
    GL_LOAD(glBindBuffer);
    GL_LOAD(glBindBufferBase);
    GL_LOAD(glBindFramebuffer);
    GL_LOAD(glBindTexture);
    GL_LOAD(glBindVertexArray);
    GL_LOAD(glBlendEquationSeparate);
    GL_LOAD(glBlendFuncSeparate);
    GL_LOAD(glBufferData);
    GL_LOAD(glBufferSubData);
    GL_LOAD(glCheckFramebufferStatus);
    GL_LOAD(glClear);
    GL_LOAD(glClearColor);
    GL_LOAD(glClearDepthf);
    GL_LOAD(glCompileShader);
    GL_LOAD(glCreateProgram);
    GL_LOAD(glCreateShader);
    GL_LOAD(glCullFace);
    GL_LOAD(glDeleteBuffers);
    GL_LOAD(glDeleteFramebuffers);
    GL_LOAD(glDeleteProgram);
    GL_LOAD(glDeleteShader);
    GL_LOAD(glDeleteTextures);
    GL_LOAD(glDeleteVertexArrays);
    GL_LOAD(glDepthFunc);
    GL_LOAD(glDepthMask);
    GL_LOAD(glDisable);
    GL_LOAD(glDisableVertexAttribArray);
    GL_LOAD(glDrawArrays);
    GL_LOAD(glDrawElements);
    GL_LOAD(glDrawElementsInstanced);
    GL_LOAD(glEnable);
    GL_LOAD(glEnableVertexAttribArray);
    GL_LOAD(glFramebufferTexture2D);
    GL_LOAD(glGenBuffers);
    GL_LOAD(glGenFramebuffers);
    GL_LOAD(glGenTextures);
    GL_LOAD(glGenVertexArrays);
    GL_LOAD(glGetAttribLocation);
    GL_LOAD(glGetError);
    GL_LOAD(glGetProgramInfoLog);
    GL_LOAD(glGetProgramiv);
    GL_LOAD(glGetShaderInfoLog);
    GL_LOAD(glGetShaderiv);
    GL_LOAD(glGetUniformLocation);
    GL_LOAD(glLinkProgram);
    GL_LOAD(glPixelStorei);
    GL_LOAD(glShaderSource);
    GL_LOAD(glTexImage2D);
    GL_LOAD(glTexParameteri);
    GL_LOAD(glTexSubImage2D);
    GL_LOAD(glUniform1f);
    GL_LOAD(glUniform1fv);
    GL_LOAD(glUniform1i);
    GL_LOAD(glUniform3fv);
    GL_LOAD(glUniform4fv);
    GL_LOAD(glUniformMatrix4fv);
    GL_LOAD(glUseProgram);
    GL_LOAD(glVertexAttribDivisor);
    GL_LOAD(glVertexAttribPointer);
    GL_LOAD(glViewport);
    return 1;
}

/* ------------------------------------------------------------------ */
/*  Helper: compile a single GL shader                                 */
/* ------------------------------------------------------------------ */

static GLuint gl_compile_shader(GLenum type, const char *source, size_t size) {
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;

    const GLint len = (GLint)size;
    glShaderSource(shader, 1, &source, &len);
    glCompileShader(shader);

    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[4096];
        GLsizei logLen;
        glGetShaderInfoLog(shader, sizeof(log), &logLen, log);
        fprintf(stderr, "GL shader compile error:\n%.*s\n", (int)logLen, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/* ------------------------------------------------------------------ */
/*  Helper: link a GL program                                          */
/* ------------------------------------------------------------------ */

static GLuint gl_link_program(GLuint vertShader, GLuint fragShader) {
    GLuint program = glCreateProgram();
    if (!program) return 0;

    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);

    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[4096];
        GLsizei logLen;
        glGetProgramInfoLog(program, sizeof(log), &logLen, log);
        fprintf(stderr, "GL program link error:\n%.*s\n", (int)logLen, log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

/* ------------------------------------------------------------------ */
/*  Helper: set up vertex attrib pointers                              */
/* ------------------------------------------------------------------ */

static void gl_setup_vertex_attribs(VertexFormat format, GLuint program) {
    switch (format) {
        case VERTEX_FORMAT_FULL: {
            GLint loc = glGetAttribLocation(program, "position");
            if (loc >= 0) {
                glEnableVertexAttribArray((GLuint)loc);
                glVertexAttribPointer((GLuint)loc, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, position));
            }
            loc = glGetAttribLocation(program, "normal");
            if (loc >= 0) {
                glEnableVertexAttribArray((GLuint)loc);
                glVertexAttribPointer((GLuint)loc, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, normal));
            }
            loc = glGetAttribLocation(program, "tangent");
            if (loc >= 0) {
                glEnableVertexAttribArray((GLuint)loc);
                glVertexAttribPointer((GLuint)loc, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, tangent));
            }
            loc = glGetAttribLocation(program, "bitangent");
            if (loc >= 0) {
                glEnableVertexAttribArray((GLuint)loc);
                glVertexAttribPointer((GLuint)loc, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, bitangent));
            }
            loc = glGetAttribLocation(program, "texcoord");
            if (loc >= 0) {
                glEnableVertexAttribArray((GLuint)loc);
                glVertexAttribPointer((GLuint)loc, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, texCoords));
            }
            break;
        }
        case VERTEX_FORMAT_POS_UV: {
            GLint loc = glGetAttribLocation(program, "in_position");
            if (loc >= 0) {
                glEnableVertexAttribArray((GLuint)loc);
                glVertexAttribPointer((GLuint)loc, 3, GL_FLOAT, GL_FALSE, 20, (void *)0);
            }
            loc = glGetAttribLocation(program, "in_texcoord");
            if (loc >= 0) {
                glEnableVertexAttribArray((GLuint)loc);
                glVertexAttribPointer((GLuint)loc, 2, GL_FLOAT, GL_FALSE, 20, (void *)12);
            }
            break;
        }
        case VERTEX_FORMAT_POS_COLOR: {
            GLint loc = glGetAttribLocation(program, "position");
            if (loc >= 0) {
                glEnableVertexAttribArray((GLuint)loc);
                glVertexAttribPointer((GLuint)loc, 3, GL_FLOAT, GL_FALSE, 24, (void *)0);
            }
            loc = glGetAttribLocation(program, "color");
            if (loc >= 0) {
                glEnableVertexAttribArray((GLuint)loc);
                glVertexAttribPointer((GLuint)loc, 4, GL_FLOAT, GL_FALSE, 24, (void *)12);
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Helper: apply raster state                                         */
/* ------------------------------------------------------------------ */

static void gl_apply_raster_state(RasterState state) {
    if (state.depthTest) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(state.depthWrite ? GL_TRUE : GL_FALSE);

    if (state.backfaceCulling) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    } else {
        glDisable(GL_CULL_FACE);
    }

    if (state.blendMode != BLEND_NONE) {
        glEnable(GL_BLEND);
        switch (state.blendMode) {
            case BLEND_ALPHA:
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                break;
            case BLEND_ADD:
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                break;
            case BLEND_PREMULT:
                glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                break;
            default:
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                break;
        }
    } else {
        glDisable(GL_BLEND);
    }
}

/* ------------------------------------------------------------------ */
/*  Shader management                                                  */
/* ------------------------------------------------------------------ */

Shader graphics_createshader(ShaderStage stage, const char *source, size_t size,
                             const char **defines, size_t defineCount) {
    (void)defines;
    (void)defineCount;

    GLenum type = (stage == SHADER_STAGE_VERTEX) ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
    GLuint glShader = gl_compile_shader(type, source, size);
    if (!glShader) return NULL;

    GLShader *s = (GLShader *)calloc(1, sizeof(GLShader));
    if (!s) { glDeleteShader(glShader); return NULL; }

    if (stage == SHADER_STAGE_VERTEX) {
        s->vertShader = glShader;
    } else {
        s->fragShader = glShader;
    }
    return (Shader)s;
}

void graphics_destroyshader(Shader shader) {
    if (!shader) return;
    GLShader *s = (GLShader *)shader;
    if (s->vertShader) glDeleteShader(s->vertShader);
    if (s->fragShader) glDeleteShader(s->fragShader);
    free(s);
}

/* ------------------------------------------------------------------ */
/*  Material management                                                */
/* ------------------------------------------------------------------ */

Material graphics_creatematerial(Shader shader) {
    GLMaterial *m = (GLMaterial *)calloc(1, sizeof(GLMaterial));
    if (!m) return NULL;
    m->shader = shader;
    glGenBuffers(1, &m->uniformBuffer);
    glBindBuffer(GL_UNIFORM_BUFFER, m->uniformBuffer);
    glBufferData(GL_UNIFORM_BUFFER, GL_MATERIAL_BUFFER_SIZE, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    m->dirty = 1;
    return (Material)m;
}

void graphics_destroymaterial(Material mat) {
    if (!mat) return;
    GLMaterial *m = (GLMaterial *)mat;
    if (m->uniformBuffer) glDeleteBuffers(1, &m->uniformBuffer);
    free(m);
}

void graphics_material_set_texture(Material mat, const char *name, Texture tex) {
    GLMaterial *m = (GLMaterial *)mat;
    if (!m || !name || !tex) return;
    if (m->textureCount < GL_MATERIAL_TEXTURE_COUNT) {
        m->textures[m->textureCount++] = (GLTexture *)tex;
        m->dirty = 1;
    }
}

void graphics_material_set_float(Material mat, const char *name, float value) {
    GLMaterial *m = (GLMaterial *)mat;
    if (!m || !name) return;
    if (m->floatCount < GL_MATERIAL_FLOAT_COUNT) {
        m->floats[m->floatCount++] = value;
        m->dirty = 1;
    }
}

void graphics_material_set_vec3(Material mat, const char *name,
                                float x, float y, float z) {
    GLMaterial *m = (GLMaterial *)mat;
    if (!m || !name) return;
    if (m->vec3Count < GL_MATERIAL_VEC3_COUNT) {
        size_t idx = m->vec3Count * 3;
        m->vec3s[idx + 0] = x;
        m->vec3s[idx + 1] = y;
        m->vec3s[idx + 2] = z;
        m->vec3Count++;
        m->dirty = 1;
    }
}

void graphics_material_set_mat4(Material mat, const float *matrix4x4) {
    GLMaterial *m = (GLMaterial *)mat;
    if (!m || !matrix4x4) return;
    memcpy(m->mat4, matrix4x4, sizeof(m->mat4));
    m->hasMat4 = 1;
    m->dirty = 1;
}

static void gl_material_pack(GLMaterial *m) {
    if (!m || !m->dirty) return;

    float data[GL_MATERIAL_BUFFER_SIZE / 4] = {0};
    size_t offset = 0;

    for (size_t i = 0; i < GL_MATERIAL_FLOAT_COUNT; i++) {
        data[offset++] = (i < m->floatCount) ? m->floats[i] : 0.0f;
    }

    for (size_t i = 0; i < GL_MATERIAL_VEC3_COUNT; i++) {
        if (i < m->vec3Count) {
            size_t srcIdx = i * 3;
            data[offset++] = m->vec3s[srcIdx + 0];
            data[offset++] = m->vec3s[srcIdx + 1];
            data[offset++] = m->vec3s[srcIdx + 2];
            data[offset++] = 0.0f;
        } else {
            data[offset++] = 0.0f;
            data[offset++] = 0.0f;
            data[offset++] = 0.0f;
            data[offset++] = 0.0f;
        }
    }

    if (m->hasMat4) {
        memcpy(&data[offset], m->mat4, 16 * sizeof(float));
    }

    glBindBuffer(GL_UNIFORM_BUFFER, m->uniformBuffer);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, GL_MATERIAL_BUFFER_SIZE, data);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    m->dirty = 0;
}

void graphics_setmaterial(Material mat) {
    if (!mat) return;
    GLMaterial *m = (GLMaterial *)mat;
    gl_material_pack(m);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, m->uniformBuffer);

    for (size_t i = 0; i < m->textureCount; i++) {
        if (m->textures[i]) {
            glActiveTexture((GLenum)(GL_TEXTURE0 + i));
            glBindTexture(GL_TEXTURE_2D, m->textures[i]->texture);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Buffer management                                                  */
/* ------------------------------------------------------------------ */

Buffer graphics_createvertexbuffer(const void *data, size_t size) {
    GLBuffer *buf = (GLBuffer *)calloc(1, sizeof(GLBuffer));
    if (!buf) return NULL;

    glGenVertexArrays(1, &buf->vao);
    glGenBuffers(1, &buf->vbo);

    glBindVertexArray(buf->vao);
    glBindBuffer(GL_ARRAY_BUFFER, buf->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)size, data, GL_STATIC_DRAW);
    glBindVertexArray(0);

    buf->vertexCapacity = size;
    buf->vertexSize = size;
    return (Buffer)buf;
}

Buffer graphics_createindexbuffer(const void *data, size_t size) {
    GLBuffer *buf = (GLBuffer *)calloc(1, sizeof(GLBuffer));
    if (!buf) return NULL;

    glGenVertexArrays(1, &buf->vao);
    glGenBuffers(1, &buf->vbo);
    glGenBuffers(1, &buf->ebo);

    glBindVertexArray(buf->vao);
    glBindBuffer(GL_ARRAY_BUFFER, buf->vbo);
    glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)size, data, GL_STATIC_DRAW);
    glBindVertexArray(0);

    buf->indexCapacity = size;
    buf->indexSize = size;
    return (Buffer)buf;
}

Buffer graphics_createuniformbuffer(size_t size) {
    GLBuffer *buf = (GLBuffer *)calloc(1, sizeof(GLBuffer));
    if (!buf) return NULL;

    glGenBuffers(1, &buf->vbo);
    glBindBuffer(GL_UNIFORM_BUFFER, buf->vbo);
    glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)size, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    buf->vertexSize = size;
    return (Buffer)buf;
}

void graphics_updatebuffer(Buffer buf, const void *data, size_t size) {
    if (!buf || !data) return;
    GLBuffer *b = (GLBuffer *)buf;

    if (b->ebo) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->ebo);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr)size, data);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    } else if (b->vao) {
        glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)size, data);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    } else {
        glBindBuffer(GL_UNIFORM_BUFFER, b->vbo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)size, data);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }
}

void graphics_destroybuffer(Buffer buf) {
    if (!buf) return;
    GLBuffer *b = (GLBuffer *)buf;
    if (b->vao) glDeleteVertexArrays(1, &b->vao);
    if (b->vbo) glDeleteBuffers(1, &b->vbo);
    if (b->ebo) glDeleteBuffers(1, &b->ebo);
    free(b);
}

void graphics_binduniformbuffer(Buffer buf, unsigned slot) {
    if (!buf) return;
    GLBuffer *b = (GLBuffer *)buf;
    glBindBufferBase(GL_UNIFORM_BUFFER, (GLuint)slot, b->vbo);
}

/* ------------------------------------------------------------------ */
/*  Texture management                                                 */
/* ------------------------------------------------------------------ */

Texture graphics_createtexture(Texture src) { return src; }

Texture graphics_createtexture_rgba(int width, int height, const unsigned char *pixels) {
    if (width <= 0 || height <= 0) return NULL;

    GLTexture *t = (GLTexture *)calloc(1, sizeof(GLTexture));
    if (!t) return NULL;

    glGenTextures(1, &t->texture);
    glBindTexture(GL_TEXTURE_2D, t->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    t->width = width;
    t->height = height;
    return (Texture)t;
}

void graphics_updatetexture(Texture tex, int x, int y, int width, int height,
                            const unsigned char *pixels) {
    if (!tex || !pixels) return;
    GLTexture *t = (GLTexture *)tex;
    glBindTexture(GL_TEXTURE_2D, t->texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void graphics_destroytexture(Texture tex) {
    if (!tex) return;
    GLTexture *t = (GLTexture *)tex;
    glDeleteTextures(1, &t->texture);
    free(t);
}

void graphics_bindtexture(Texture tex, unsigned slot) {
    if (!tex) return;
    GLTexture *t = (GLTexture *)tex;
    glActiveTexture((GLenum)(GL_TEXTURE0 + slot));
    glBindTexture(GL_TEXTURE_2D, t->texture);
}

/* ------------------------------------------------------------------ */
/*  Pipeline management                                                */
/* ------------------------------------------------------------------ */

Pipeline graphics_createpipeline(Shader vertShader, Shader fragShader,
                                 VertexFormat format, RasterState state) {
    if (!vertShader || !fragShader) return NULL;

    GLShader *vs = (GLShader *)vertShader;
    GLShader *fs = (GLShader *)fragShader;
    GLuint program = gl_link_program(vs->vertShader, fs->fragShader);
    if (!program) return NULL;

    GLPipeline *p = (GLPipeline *)calloc(1, sizeof(GLPipeline));
    if (!p) { glDeleteProgram(program); return NULL; }

    p->program = program;
    p->format = format;
    p->state = state;
    p->loc_model = glGetUniformLocation(program, "model");
    p->loc_view = glGetUniformLocation(program, "view");
    p->loc_projection = glGetUniformLocation(program, "projection");
    p->loc_normalMatrix = glGetUniformLocation(program, "normalMatrix");
    p->loc_tex = glGetUniformLocation(program, "tex");
    p->loc_color = glGetUniformLocation(program, "color");
    return (Pipeline)p;
}

void graphics_bindpipeline(Pipeline pipeline) {
    if (!pipeline) return;
    GLPipeline *p = (GLPipeline *)pipeline;

    if (g_currentProgram != p->program) {
        glUseProgram(p->program);
        g_currentProgram = p->program;
    }

    gl_apply_raster_state(p->state);

    if (g_uniformBuffer) {
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, g_uniformBuffer);
    }
}

void graphics_destroypipeline(Pipeline pipeline) {
    if (!pipeline) return;
    GLPipeline *p = (GLPipeline *)pipeline;
    if (p->program) glDeleteProgram(p->program);
    free(p);
}

/* ------------------------------------------------------------------ */
/*  Render pass management                                             */
/* ------------------------------------------------------------------ */

RenderPass graphics_createpass(const char *name, RasterState state) {
    (void)name;
    GLRenderPass *pass = (GLRenderPass *)calloc(1, sizeof(GLRenderPass));
    if (!pass) return NULL;
    pass->state = state;
    return (RenderPass)pass;
}

void graphics_pass_set_color_texture(RenderPass pass, Texture tex, unsigned slot) {
    if (!pass || !tex) return;
    GLRenderPass *rp = (GLRenderPass *)pass;
    GLTexture *t = (GLTexture *)tex;
    if (slot < GL_MAX_COLOR_ATTACHMENTS) {
        rp->colorTextures[slot] = t;
        if ((int)slot + 1 > rp->colorCount) rp->colorCount = (int)slot + 1;
    }
}

void graphics_pass_set_depth_texture(RenderPass pass, Texture tex) {
    if (!pass || !tex) return;
    GLRenderPass *rp = (GLRenderPass *)pass;
    rp->depthTexture = (GLTexture *)tex;
    rp->hasDepth = 1;
}

static void gl_pass_build(GLRenderPass *rp) {
    if (rp->initialized) return;

    glGenFramebuffers(1, &rp->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, rp->fbo);

    for (int i = 0; i < rp->colorCount; i++) {
        if (rp->colorTextures[i]) {
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                (GLenum)(GL_COLOR_ATTACHMENT0 + i),
                GL_TEXTURE_2D, rp->colorTextures[i]->texture, 0);
        }
    }

    if (rp->hasDepth && rp->depthTexture) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
            GL_TEXTURE_2D, rp->depthTexture->texture, 0);
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "GL: framebuffer incomplete (status 0x%x)\n", status);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    rp->initialized = 1;
}

void graphics_beginpass(RenderPass pass) {
    if (pass) {
        GLRenderPass *rp = (GLRenderPass *)pass;
        gl_pass_build(rp);
        glBindFramebuffer(GL_FRAMEBUFFER, rp->fbo);
        g_currentFBO = rp->fbo;

        int width = g_windowWidth;
        int height = g_windowHeight;
        if (rp->colorCount > 0 && rp->colorTextures[0]) {
            width = rp->colorTextures[0]->width;
            height = rp->colorTextures[0]->height;
        }
        glViewport(0, 0, width, height);

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        GLbitfield clearBits = GL_COLOR_BUFFER_BIT;
        if (rp->hasDepth) clearBits |= GL_DEPTH_BUFFER_BIT;
        glClear(clearBits);
        g_inPass = 1;
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        g_currentFBO = 0;
        glViewport(0, 0, g_windowWidth, g_windowHeight);
        g_inPass = 1;
    }
}

void graphics_endpass(RenderPass pass) {
    (void)pass;
    g_inPass = 0;
}

void graphics_destroypass(RenderPass pass) {
    if (!pass) return;
    GLRenderPass *rp = (GLRenderPass *)pass;
    if (rp->fbo) glDeleteFramebuffers(1, &rp->fbo);
    free(rp);
}

/* ------------------------------------------------------------------ */
/*  Shader variants                                                    */
/* ------------------------------------------------------------------ */

Shader graphics_get_shader_variant(Shader base, const char **defines, size_t defineCount) {
    (void)defines;
    (void)defineCount;
    return base;
}

/* ------------------------------------------------------------------ */
/*  Core graphics functions                                            */
/* ------------------------------------------------------------------ */

void graphics_init() {
    void graphics_shutdown(void);

    g_window = (SDL_Window *)window_getwindow();
    if (!g_window) {
        fprintf(stderr, "GL: No SDL window available\n");
        exit(EXIT_FAILURE);
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    g_context = SDL_GL_CreateContext(g_window);
    if (!g_context) {
        fprintf(stderr, "GL: Failed to create context: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    if (!gl_load_functions()) {
        fprintf(stderr, "GL: Failed to load OpenGL functions\n");
        exit(EXIT_FAILURE);
    }

    atexit(graphics_shutdown);

    window_getwindowsizeinpixels(&g_windowWidth, &g_windowHeight);
    printf("OpenGL initialized: %dx%d\n", g_windowWidth, g_windowHeight);

    glGenBuffers(1, &g_uniformBuffer);
    glBindBuffer(GL_UNIFORM_BUFFER, g_uniformBuffer);
    glBufferData(GL_UNIFORM_BUFFER, GL_UNIFORM_BUFFER_SIZE, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(CLEAR_COLOR[0], CLEAR_COLOR[1], CLEAR_COLOR[2], CLEAR_COLOR[3]);

    /* Default 3D shaders (match shaders/default3d.vert + shaders/default.frag) */
    const char *default3dVert =
        "#version 330\n"
        "uniform mat4 model;\n"
        "uniform mat4 view;\n"
        "uniform mat4 projection;\n"
        "uniform mat4 normalMatrix;\n"
        "in vec3 position;\n"
        "in vec3 normal;\n"
        "in vec3 tangent;\n"
        "in vec2 texcoord;\n"
        "out vec3 Normal;\n"
        "out vec3 Tangent;\n"
        "out vec2 TexCoord;\n"
        "void main() {\n"
        "    gl_Position = projection * view * model * vec4(position, 1.0);\n"
        "    Normal = normal;\n"
        "    Tangent = tangent;\n"
        "    TexCoord = texcoord;\n"
        "}\n";

    const char *default3dFrag =
        "#version 330\n"
        "in vec3 Normal;\n"
        "in vec3 Tangent;\n"
        "in vec2 TexCoord;\n"
        "out vec4 FragColor;\n"
        "void main() {\n"
        "    FragColor = vec4(Normal * 0.5 + 0.5, 1.0);\n"
        "}\n";

    GLuint dv = gl_compile_shader(GL_VERTEX_SHADER, default3dVert, strlen(default3dVert));
    GLuint df = gl_compile_shader(GL_FRAGMENT_SHADER, default3dFrag, strlen(default3dFrag));

    g_defaultVertShader = (GLShader *)calloc(1, sizeof(GLShader));
    g_defaultFragShader = (GLShader *)calloc(1, sizeof(GLShader));
    g_defaultVertShader->vertShader = dv;
    g_defaultFragShader->fragShader = df;

    RasterState defaultState = {1, 1, 1, BLEND_NONE};
    g_defaultPipeline = (GLPipeline *)graphics_createpipeline(
        (Shader)g_defaultVertShader, (Shader)g_defaultFragShader,
        VERTEX_FORMAT_FULL, defaultState);

    /* Text shaders (match shaders/text.vert + shaders/text.frag style) */
    const char *textVert =
        "#version 330\n"
        "in vec3 position;\n"
        "in vec2 texcoord;\n"
        "out vec2 TexCoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(position, 1.0);\n"
        "    TexCoord = texcoord;\n"
        "}\n";

    const char *textFrag =
        "#version 330\n"
        "uniform sampler2D tex;\n"
        "uniform vec4 color;\n"
        "in vec2 TexCoord;\n"
        "out vec4 FragColor;\n"
        "void main() {\n"
        "    float alpha = texture(tex, TexCoord).a;\n"
        "    FragColor = vec4(1.0, 1.0, 1.0, alpha);\n"
        "}\n";

    GLuint tv = gl_compile_shader(GL_VERTEX_SHADER, textVert, strlen(textVert));
    GLuint tf = gl_compile_shader(GL_FRAGMENT_SHADER, textFrag, strlen(textFrag));

    g_textVertShader = (GLShader *)calloc(1, sizeof(GLShader));
    g_textFragShader = (GLShader *)calloc(1, sizeof(GLShader));
    g_textVertShader->vertShader = tv;
    g_textFragShader->fragShader = tf;

    RasterState textState = {0, 0, 0, BLEND_ALPHA};
    g_textPipeline = (GLPipeline *)graphics_createpipeline(
        (Shader)g_textVertShader, (Shader)g_textFragShader,
        VERTEX_FORMAT_POS_UV, textState);
}

void graphics_get_text_shaders(Shader *out_vert, Shader *out_frag) {
    if (out_vert) *out_vert = (Shader)g_textVertShader;
    if (out_frag) *out_frag = (Shader)g_textFragShader;
}

void graphics_shutdown() {
    if (g_uniformBuffer) glDeleteBuffers(1, &g_uniformBuffer);
    if (g_defaultPipeline) graphics_destroypipeline((Pipeline)g_defaultPipeline);
    if (g_textPipeline) graphics_destroypipeline((Pipeline)g_textPipeline);
    if (g_defaultVertShader) graphics_destroyshader((Shader)g_defaultVertShader);
    if (g_defaultFragShader) graphics_destroyshader((Shader)g_defaultFragShader);
    if (g_textVertShader) graphics_destroyshader((Shader)g_textVertShader);
    if (g_textFragShader) graphics_destroyshader((Shader)g_textFragShader);

    if (g_context) {
        SDL_GL_DestroyContext(g_context);
        g_context = NULL;
    }
    printf("OpenGL graphics backend shut down\n");
}

void graphics_resize() {
    window_getwindowsizeinpixels(&g_windowWidth, &g_windowHeight);
    glViewport(0, 0, g_windowWidth, g_windowHeight);
    printf("Graphics resized to %dx%d\n", g_windowWidth, g_windowHeight);
}

int graphics_isminimized() { return g_minimized; }

void graphics_predraw() {
    if (g_minimized) return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_currentFBO = 0;
    glViewport(0, 0, g_windowWidth, g_windowHeight);
    glClearColor(CLEAR_COLOR[0], CLEAR_COLOR[1], CLEAR_COLOR[2], CLEAR_COLOR[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g_currentProgram = 0;
    g_inPass = 1;
}

void graphics_postdraw() { g_inPass = 0; }

void graphics_present() {
    if (g_minimized) return;
    SDL_GL_SwapWindow(g_window);
}

void graphics_setshader(Shader vertShader, Shader fragShader) {
    (void)vertShader;
    (void)fragShader;
}

/* ------------------------------------------------------------------ */
/*  Model loading and drawing                                          */
/* ------------------------------------------------------------------ */

Model *graphics_loadmodel(const char *filepath) {
    Model *model = model_load(filepath);
    if (!model) return NULL;

    for (uint32_t i = 0; i < model->meshCount; i++) {
        Mesh *mesh = &model->meshes[i];
        if (mesh->vertexCount > 0) {
            mesh->vertexBuffer = graphics_createvertexbuffer(
                mesh->vertices, mesh->vertexCount * sizeof(Vertex));
        }
        if (mesh->indexCount > 0) {
            mesh->indexBuffer = graphics_createindexbuffer(
                mesh->indices, mesh->indexCount * sizeof(uint32_t));
        }
    }
    return model;
}

void graphics_destroymodel(Model *model) {
    if (!model) return;
    for (uint32_t i = 0; i < model->meshCount; i++) {
        Mesh *mesh = &model->meshes[i];
        if (mesh->vertexBuffer) graphics_destroybuffer(mesh->vertexBuffer);
        if (mesh->indexBuffer) graphics_destroybuffer(mesh->indexBuffer);
    }
    model_destroy(model);
}

void graphics_drawmodel(Model *model, Material mat, const float *transform4x4) {
    if (!model) return;

    GLMaterial *mm = (GLMaterial *)mat;
    Shader vertShader = mm ? mm->shader : (Shader)g_defaultVertShader;
    Shader fragShader = mm ? mm->shader : (Shader)g_defaultFragShader;
    if (!vertShader || !fragShader) return;

    RasterState state = {.depthWrite = 1, .depthTest = 1, .backfaceCulling = 1, .blendMode = BLEND_NONE};
    Pipeline p = graphics_createpipeline(vertShader, fragShader, VERTEX_FORMAT_FULL, state);
    if (p) graphics_bindpipeline(p);

    if (transform4x4 && g_uniformBuffer) {
        glBindBuffer(GL_UNIFORM_BUFFER, g_uniformBuffer);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, 64, transform4x4);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, g_uniformBuffer);
    }

    if (mat) graphics_setmaterial(mat);

    for (uint32_t i = 0; i < model->meshCount; i++) {
        Mesh *mesh = &model->meshes[i];
        if (!mesh->vertexBuffer) continue;

        GLBuffer *vb = (GLBuffer *)mesh->vertexBuffer;
        glBindVertexArray(vb->vao);
        glBindBuffer(GL_ARRAY_BUFFER, vb->vbo);
        gl_setup_vertex_attribs(VERTEX_FORMAT_FULL, g_currentProgram);

        if (mesh->indexBuffer && mesh->indexCount > 0) {
            GLBuffer *ib = (GLBuffer *)mesh->indexBuffer;
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->ebo);
            glDrawElements(GL_TRIANGLES, (GLsizei)mesh->indexCount, GL_UNSIGNED_INT, NULL);
        } else if (mesh->vertexCount > 0) {
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)mesh->vertexCount);
        }
        glBindVertexArray(0);
    }
}

void graphics_draw_instanced(Model *model, Material mat, const float *transforms4x4, size_t count) {
    if (!model) return;

    GLMaterial *mm = (GLMaterial *)mat;
    Shader vertShader = mm ? mm->shader : (Shader)g_defaultVertShader;
    Shader fragShader = mm ? mm->shader : (Shader)g_defaultFragShader;
    if (!vertShader || !fragShader) return;

    RasterState state = {.depthWrite = 1, .depthTest = 1, .backfaceCulling = 1, .blendMode = BLEND_NONE};
    Pipeline p = graphics_createpipeline(vertShader, fragShader, VERTEX_FORMAT_FULL, state);
    if (p) graphics_bindpipeline(p);

    if (transforms4x4 && g_uniformBuffer) {
        glBindBuffer(GL_UNIFORM_BUFFER, g_uniformBuffer);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, 64, transforms4x4);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, g_uniformBuffer);
    }

    if (mat) graphics_setmaterial(mat);

    for (uint32_t i = 0; i < model->meshCount; i++) {
        Mesh *mesh = &model->meshes[i];
        if (!mesh->vertexBuffer) continue;

        GLBuffer *vb = (GLBuffer *)mesh->vertexBuffer;
        glBindVertexArray(vb->vao);
        glBindBuffer(GL_ARRAY_BUFFER, vb->vbo);
        gl_setup_vertex_attribs(VERTEX_FORMAT_FULL, g_currentProgram);

        if (mesh->indexBuffer && mesh->indexCount > 0) {
            GLBuffer *ib = (GLBuffer *)mesh->indexBuffer;
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->ebo);
            glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)mesh->indexCount, GL_UNSIGNED_INT, NULL, (GLsizei)count);
        }
        glBindVertexArray(0);
    }
}

void graphics_draw_buffers(Buffer vertexBuffer, Buffer indexBuffer, size_t indexCount,
                           size_t firstIndex, Material mat, const float *transform4x4) {
    if (!vertexBuffer) return;

    GLBuffer *vb = (GLBuffer *)vertexBuffer;
    glBindVertexArray(vb->vao);
    glBindBuffer(GL_ARRAY_BUFFER, vb->vbo);

    if (g_currentProgram) {
        GLint loc = glGetAttribLocation(g_currentProgram, "in_position");
        if (loc >= 0) {
            glEnableVertexAttribArray((GLuint)loc);
            glVertexAttribPointer((GLuint)loc, 3, GL_FLOAT, GL_FALSE, 20, (void *)0);
        }
        loc = glGetAttribLocation(g_currentProgram, "in_texcoord");
        if (loc >= 0) {
            glEnableVertexAttribArray((GLuint)loc);
            glVertexAttribPointer((GLuint)loc, 2, GL_FLOAT, GL_FALSE, 20, (void *)12);
        }
    }

    if (transform4x4 && g_uniformBuffer) {
        glBindBuffer(GL_UNIFORM_BUFFER, g_uniformBuffer);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, 64, transform4x4);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, g_uniformBuffer);
    }

    if (mat) graphics_setmaterial(mat);

    if (indexBuffer && indexCount > 0) {
        GLBuffer *ib = (GLBuffer *)indexBuffer;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->ebo);
        glDrawElements(GL_TRIANGLES, (GLsizei)indexCount, GL_UNSIGNED_INT,
                       (void *)(uintptr_t)(firstIndex * sizeof(uint32_t)));
    }

    glBindVertexArray(0);
}