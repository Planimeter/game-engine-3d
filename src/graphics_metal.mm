/* Copyright Planimeter. All Rights Reserved. */

#include "framework.h"
#include "filesystem.h"
#include "graphics.h"
#include "window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#import <Metal/Metal.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include "SDL3/SDL_metal.h"
#include "graphics_metal_helpers.h"

static const float CLEAR_COLOR[4] = {0.01f, 0.01f, 0.033f, 1.0f};

typedef struct MetalShader MetalShader;
typedef struct MetalPipeline MetalPipeline;
typedef struct MetalBuffer MetalBuffer;
typedef struct MetalTexture MetalTexture;

struct MetalShader {
    id<MTLFunction> vertFunc;
    id<MTLFunction> fragFunc;
    id<MTLLibrary> library;
};

struct MetalPipeline {
    id<MTLRenderPipelineState> pipelineState;
    id<MTLDepthStencilState> depthState;
    VertexFormat vertexFormat;
    RasterState rasterState;
};

struct MetalBuffer {
    id<MTLBuffer> buffer;
    size_t size;
};

struct MetalTexture {
    id<MTLTexture> texture;
    int width;
    int height;
};

// Global state
static id<MTLDevice> g_device = nil;
static id<MTLCommandQueue> g_commandQueue = nil;
static CAMetalLayer *g_metalLayer = nil;

static MetalPipeline **g_pipelines = NULL;
static int g_pipelineCount = 0;
static int g_pipelineCapacity = 0;

static MetalBuffer **g_buffers = NULL;
static int g_bufferCount = 0;
static int g_bufferCapacity = 0;

static MetalTexture **g_textures = NULL;
static int g_textureCount = 0;
static int g_textureCapacity = 0;

/* Frame state */
static id<MTLCommandBuffer> g_currentCommandBuffer = nil;
static id<MTLRenderCommandEncoder> g_currentEncoder = nil;
static int g_inPass = 0;
static MetalPipeline *g_currentPipeline = NULL;
static id<CAMetalDrawable> g_currentDrawable = nil;

/* Default sampler */
static id<MTLSamplerState> g_defaultSampler = nil;

/* Uniform buffer */
#define UNIFORM_BUFFER_SIZE 4096
static id<MTLBuffer> g_uniformBuffer = nil;

static int g_windowWidth = 640;
static int g_windowHeight = 480;
static int g_minimized = 0;

/* Default shaders */
static MetalShader *g_defaultVertShader = NULL;
static MetalShader *g_defaultFragShader = NULL;

/* Text rendering shaders (embedded MSL, used when SPIR-V not available) */
static MetalShader *g_textVertShader = NULL;
static MetalShader *g_textFragShader = NULL;

static MTLVertexDescriptor *make_vertex_descriptor(VertexFormat format) {
    MTLVertexDescriptor *vd = [[MTLVertexDescriptor alloc] init];

    switch (format) {
        case VERTEX_FORMAT_FULL: {
            vd.attributes[0].format = MTLVertexFormatFloat3;
            vd.attributes[0].offset = offsetof(Vertex, position);
            vd.attributes[0].bufferIndex = 0;

            vd.attributes[1].format = MTLVertexFormatFloat3;
            vd.attributes[1].offset = offsetof(Vertex, normal);
            vd.attributes[1].bufferIndex = 0;

            vd.attributes[2].format = MTLVertexFormatFloat3;
            vd.attributes[2].offset = offsetof(Vertex, tangent);
            vd.attributes[2].bufferIndex = 0;

            vd.attributes[3].format = MTLVertexFormatFloat3;
            vd.attributes[3].offset = offsetof(Vertex, bitangent);
            vd.attributes[3].bufferIndex = 0;

            vd.attributes[4].format = MTLVertexFormatFloat2;
            vd.attributes[4].offset = offsetof(Vertex, texCoords);
            vd.attributes[4].bufferIndex = 0;

            vd.layouts[0].stride = sizeof(Vertex);
            vd.layouts[0].stepRate = 1;
            vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
            break;
        }
        case VERTEX_FORMAT_POS_UV: {
            vd.attributes[0].format = MTLVertexFormatFloat3;
            vd.attributes[0].offset = 0;
            vd.attributes[0].bufferIndex = 0;

            vd.attributes[1].format = MTLVertexFormatFloat2;
            vd.attributes[1].offset = 12;
            vd.attributes[1].bufferIndex = 0;

            vd.layouts[0].stride = 20; /* pos(12) + uv(8) */
            vd.layouts[0].stepRate = 1;
            vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
            break;
        }
        case VERTEX_FORMAT_POS_COLOR: {
            vd.attributes[0].format = MTLVertexFormatFloat3;
            vd.attributes[0].offset = 0;
            vd.attributes[0].bufferIndex = 0;

            vd.attributes[1].format = MTLVertexFormatFloat4;
            vd.attributes[1].offset = 12;
            vd.attributes[1].bufferIndex = 0;

            vd.layouts[0].stride = 24;
            vd.layouts[0].stepRate = 1;
            vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
            break;
        }
    }
    return [vd retain];
}

static id<MTLDepthStencilState> make_depth_stencil(RasterState state) {
    MTLDepthStencilDescriptor *desc = [[MTLDepthStencilDescriptor alloc] init];
    desc.depthCompareFunction = state.depthTest ? MTLCompareFunctionLess : MTLCompareFunctionAlways;
    desc.depthWriteEnabled = state.depthWrite;
    return (id<MTLDepthStencilState>)metal_device_new_depth_stencil_state(g_device, desc);
}

Shader graphics_createshader(const char *source, size_t size,
                             const char **defines, size_t defineCount) {
    (void)defines; (void)defineCount;

    MetalShader *shader = (MetalShader *)calloc(1, sizeof(MetalShader));
    if (!shader) return NULL;

    char *nullTerm = (char *)malloc(size + 1);
    memcpy(nullTerm, source, size);
    nullTerm[size] = '\0';

    NSError *error = nil;
    id<MTLLibrary> library = (id<MTLLibrary>)metal_device_new_library(g_device, nullTerm, (void **)&error);
    free(nullTerm);

    if (!library) {
        fprintf(stderr, "Metal shader compilation failed: %s\n",
                error ? [[error localizedDescription] UTF8String] : "(unknown)");
        free(shader);
        return NULL;
    }

    shader->library = library;
    shader->vertFunc = (id<MTLFunction>)metal_library_new_function(library, "vertex_main");
    shader->fragFunc = (id<MTLFunction>)metal_library_new_function(library, "fragment_main");

    if (!shader->vertFunc || !shader->fragFunc) {
        fprintf(stderr, "Metal: could not find vertex_main/fragment_main in shader\n");
        [library release];
        free(shader);
        return NULL;
    }

    return (Shader)shader;
}

void graphics_destroyshader(Shader shader) {
    if (!shader) return;
    MetalShader *s = (MetalShader *)shader;
    if (s->vertFunc) [s->vertFunc release];
    if (s->fragFunc) [s->fragFunc release];
    if (s->library) [s->library release];
    free(s);
}

/* ------------------------------------------------------------------ */
/*  Material management                                                */
/* ------------------------------------------------------------------ */

Material graphics_creatematerial(Shader shader) { return shader; }
void graphics_destroymaterial(Material mat) { (void)mat; }
void graphics_material_set_texture(Material m, const char *n, Texture t) { (void)m; (void)n; (void)t; }
void graphics_material_set_float(Material m, const char *n, float v) { (void)m; (void)n; (void)v; }
void graphics_material_set_vec3(Material m, const char *n, float x, float y, float z) { (void)m; (void)n; (void)x; (void)y; (void)z; }
void graphics_material_set_mat4(Material m, const float *matrix4x4) { (void)m; (void)matrix4x4; }
void graphics_setmaterial(Material mat) { (void)mat; }

/* ------------------------------------------------------------------ */
/*  Buffer management                                                  */
/* ------------------------------------------------------------------ */

Buffer graphics_createvertexbuffer(const void *data, size_t size) {
    id<MTLBuffer> buf = (id<MTLBuffer>)metal_device_new_buffer(g_device, size, MTLResourceStorageModeShared);
    if (!buf) return NULL;
    if (data) memcpy(metal_buffer_get_contents(buf), data, size);

    MetalBuffer *mb = (MetalBuffer *)calloc(1, sizeof(MetalBuffer));
    mb->buffer = buf;
    mb->size = size;

    if (g_bufferCount >= g_bufferCapacity) {
        g_bufferCapacity = g_bufferCapacity ? g_bufferCapacity * 2 : 16;
        MetalBuffer **tmp = (MetalBuffer **)realloc(g_buffers, g_bufferCapacity * sizeof(MetalBuffer *));
        if (!tmp) { free(mb); return NULL; }
        g_buffers = tmp;
    }
    g_buffers[g_bufferCount++] = mb;
    return (Buffer)mb;
}

Buffer graphics_createindexbuffer(const void *data, size_t size) {
    return graphics_createvertexbuffer(data, size);
}

Buffer graphics_createuniformbuffer(size_t size) {
    id<MTLBuffer> buf = (id<MTLBuffer>)metal_device_new_buffer(g_device, size, MTLResourceStorageModeShared);
    if (!buf) return NULL;

    MetalBuffer *mb = (MetalBuffer *)calloc(1, sizeof(MetalBuffer));
    mb->buffer = buf;
    mb->size = size;

    if (g_bufferCount >= g_bufferCapacity) {
        g_bufferCapacity = g_bufferCapacity ? g_bufferCapacity * 2 : 16;
        MetalBuffer **tmp = (MetalBuffer **)realloc(g_buffers, g_bufferCapacity * sizeof(MetalBuffer *));
        if (!tmp) { free(mb); return NULL; }
        g_buffers = tmp;
    }
    g_buffers[g_bufferCount++] = mb;
    return (Buffer)mb;
}

void graphics_updatebuffer(Buffer buf, const void *data, size_t size) {
    if (!buf || !data) return;
    MetalBuffer *mb = (MetalBuffer *)buf;
    metal_buffer_update(mb->buffer, data, size);
}

void graphics_destroybuffer(Buffer buf) {
    if (!buf) return;
    MetalBuffer *mb = (MetalBuffer *)buf;
    if (mb->buffer) [mb->buffer release];

    for (int i = 0; i < g_bufferCount; i++) {
        if (g_buffers[i] == mb) {
            memmove(&g_buffers[i], &g_buffers[i+1], (g_bufferCount - i - 1) * sizeof(MetalBuffer *));
            g_bufferCount--;
            break;
        }
    }
    free(mb);
}

/* ------------------------------------------------------------------ */
/*  Texture management                                                 */
/* ------------------------------------------------------------------ */

Texture graphics_createtexture(Texture src) { return src; }

Texture graphics_createtexture_rgba(int width, int height, const unsigned char *pixels) {
    MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                     width:width
                                                                                    height:height
                                                                                    mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;  /* Blit encoder handles upload, no CPU access needed */

    id<MTLTexture> tex = (id<MTLTexture>)metal_device_new_texture(g_device, desc);
    if (!tex) return NULL;

    if (pixels) {
        /* Use helper which wraps replaceBytesInRegion (avoids ObjC++ protocol dispatch) */
        metal_texture_update_region(tex, 0, 0,
                                    (unsigned long)width, (unsigned long)height,
                                    pixels, (unsigned long)(width * 4));
    }

    MetalTexture *mt = (MetalTexture *)calloc(1, sizeof(MetalTexture));
    mt->texture = tex;
    mt->width = width;
    mt->height = height;

    if (g_textureCount >= g_textureCapacity) {
        g_textureCapacity = g_textureCapacity ? g_textureCapacity * 2 : 16;
        MetalTexture **tmp = (MetalTexture **)realloc(g_textures, g_textureCapacity * sizeof(MetalTexture *));
        if (!tmp) { [tex release]; free(mt); return NULL; }
        g_textures = tmp;
    }
    g_textures[g_textureCount++] = mt;
    return (Texture)mt;
}

void graphics_updatetexture(Texture tex, int x, int y, int width, int height, const unsigned char *pixels) {
    if (!tex || !pixels) return;
    MetalTexture *mt = (MetalTexture *)tex;
    id<MTLTexture> t = mt->texture;
    NSUInteger bpr = (NSUInteger)(width * 4);
    metal_texture_update_region(t, (unsigned long)x, (unsigned long)y,
                                (unsigned long)width, (unsigned long)height,
                                pixels, (unsigned long)bpr);
}

void graphics_destroytexture(Texture tex) {
    if (!tex) return;
    MetalTexture *mt = (MetalTexture *)tex;
    if (mt->texture) [mt->texture release];

    for (int i = 0; i < g_textureCount; i++) {
        if (g_textures[i] == mt) {
            memmove(&g_textures[i], &g_textures[i+1], (g_textureCount - i - 1) * sizeof(MetalTexture *));
            g_textureCount--;
            break;
        }
    }
    free(mt);
}

void graphics_bindtexture(Texture tex, unsigned slot) {
    if (!tex || !g_currentEncoder) return;
    MetalTexture *mt = (MetalTexture *)tex;
    metal_encoder_set_fragment_texture(g_currentEncoder, mt->texture, (unsigned long)slot);
    metal_encoder_set_fragment_sampler_state(g_currentEncoder, g_defaultSampler, (unsigned long)slot);
}

/* ------------------------------------------------------------------ */
/*  Pipeline management                                                */
/* ------------------------------------------------------------------ */

Pipeline graphics_createpipeline(Shader vertShader, Shader fragShader,
                                  VertexFormat format, RasterState state) {
    if (!vertShader || !fragShader) return NULL;

    MetalShader *vs = (MetalShader *)vertShader;
    MetalShader *fs = (MetalShader *)fragShader;

    MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vs->vertFunc;
    desc.fragmentFunction = fs->fragFunc;
    desc.vertexDescriptor = make_vertex_descriptor(format);

    MTLRenderPipelineColorAttachmentDescriptor *colorAtt = [MTLRenderPipelineColorAttachmentDescriptor new];
    colorAtt.pixelFormat = MTLPixelFormatBGRA8Unorm;
    colorAtt.blendingEnabled = (state.blendMode != BLEND_NONE);

    if (colorAtt.blendingEnabled) {
        switch (state.blendMode) {
            case BLEND_ALPHA:
                colorAtt.rgbBlendOperation = MTLBlendOperationAdd;
                colorAtt.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
                colorAtt.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                colorAtt.alphaBlendOperation = MTLBlendOperationAdd;
                colorAtt.sourceAlphaBlendFactor = MTLBlendFactorOne;
                colorAtt.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                break;
            case BLEND_ADD:
                colorAtt.rgbBlendOperation = MTLBlendOperationAdd;
                colorAtt.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
                colorAtt.destinationRGBBlendFactor = MTLBlendFactorOne;
                colorAtt.alphaBlendOperation = MTLBlendOperationAdd;
                colorAtt.sourceAlphaBlendFactor = MTLBlendFactorOne;
                colorAtt.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                break;
            case BLEND_PREMULT:
                colorAtt.rgbBlendOperation = MTLBlendOperationAdd;
                colorAtt.sourceRGBBlendFactor = MTLBlendFactorOne;
                colorAtt.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                colorAtt.alphaBlendOperation = MTLBlendOperationAdd;
                colorAtt.sourceAlphaBlendFactor = MTLBlendFactorOne;
                colorAtt.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                break;
            default: break;
        }
    }

    desc.colorAttachments[0] = colorAtt;
    /* Only set depth format if depth test/write is enabled */
    if (state.depthTest || state.depthWrite) {
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    }

    NSError *error = nil;
    id<MTLRenderPipelineState> pso = (id<MTLRenderPipelineState>)metal_device_new_pipeline_state(g_device, desc, (void **)&error);
    if (!pso) {
        fprintf(stderr, "Failed to create Metal pipeline state: %s\n",
                error ? [[error localizedDescription] UTF8String] : "(unknown)");
        return NULL;
    }

    id<MTLDepthStencilState> ds = make_depth_stencil(state);

    MetalPipeline *mp = (MetalPipeline *)calloc(1, sizeof(MetalPipeline));
    mp->pipelineState = pso;
    mp->depthState = ds;
    mp->vertexFormat = format;
    mp->rasterState = state;

    if (g_pipelineCount >= g_pipelineCapacity) {
        g_pipelineCapacity = g_pipelineCapacity ? g_pipelineCapacity * 2 : 16;
        MetalPipeline **tmp = (MetalPipeline **)realloc(g_pipelines, g_pipelineCapacity * sizeof(MetalPipeline *));
        if (!tmp) { [pso release]; [ds release]; return NULL; }
        g_pipelines = tmp;
    }
    g_pipelines[g_pipelineCount++] = mp;

    return (Pipeline)mp;
}

void graphics_bindpipeline(Pipeline pipeline) {
    if (!pipeline || !g_currentEncoder) return;
    MetalPipeline *mp = (MetalPipeline *)pipeline;
    g_currentPipeline = mp;
    metal_encoder_set_render_pipeline_state(g_currentEncoder, mp->pipelineState);
    metal_encoder_set_depth_stencil_state(g_currentEncoder, mp->depthState);
}

void graphics_destroypipeline(Pipeline pipeline) {
    if (!pipeline) return;
    MetalPipeline *mp = (MetalPipeline *)pipeline;
    if (mp->pipelineState) [mp->pipelineState release];
    if (mp->depthState) [mp->depthState release];

    for (int i = 0; i < g_pipelineCount; i++) {
        if (g_pipelines[i] == mp) {
            memmove(&g_pipelines[i], &g_pipelines[i+1], (g_pipelineCount - i - 1) * sizeof(MetalPipeline *));
            g_pipelineCount--;
            break;
        }
    }
    free(mp);
}

Shader graphics_get_shader_variant(Shader base, const char **defines, size_t defineCount) {
    (void)defines; (void)defineCount;
    return base;
}

/* ------------------------------------------------------------------ */
/*  Render pass management                                             */
/* ------------------------------------------------------------------ */

RenderPass graphics_createpass(const char *name, RasterState state) {
    (void)name; (void)state;
    return (RenderPass)1;
}

void graphics_beginpass(RenderPass pass) { (void)pass; g_inPass = 1; }

void graphics_endpass(RenderPass pass) {
    (void)pass;
    if (g_currentEncoder) {
        metal_encoder_end_encoding(g_currentEncoder);
        g_currentEncoder = nil;
    }
    g_inPass = 0;
}

/* ------------------------------------------------------------------ */
/*  Core graphics functions                                            */
/* ------------------------------------------------------------------ */

void graphics_init() {
    /* Create Metal device */
    g_device = MTLCreateSystemDefaultDevice();
    if (!g_device) {
        fprintf(stderr, "Failed to create Metal device\n");
        exit(EXIT_FAILURE);
    }
    printf("Metal device: %s\n", [[g_device name] UTF8String]);

    /* Create command queue */
    g_commandQueue = (id<MTLCommandQueue>)metal_device_new_command_queue(g_device);

    /* Uniform buffer */
    g_uniformBuffer = (id<MTLBuffer>)metal_device_new_buffer(g_device, UNIFORM_BUFFER_SIZE, MTLResourceStorageModeShared);

    /* Get CAMetalLayer from SDL's Metal view */
    SDL_MetalView metalView = (SDL_MetalView)(uintptr_t)(size_t)window_get_metal_view();
    if (!metalView) {
        fprintf(stderr, "Metal: No Metal view available\n");
        exit(EXIT_FAILURE);
    }

    g_metalLayer = (CAMetalLayer *)SDL_Metal_GetLayer(metalView);
    if (!g_metalLayer) {
        fprintf(stderr, "Metal: Could not get CAMetalLayer from SDL\n");
        exit(EXIT_FAILURE);
    }

    /* Configure the Metal layer */
    g_metalLayer.device = g_device;
    g_metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;

    int w, h;
    window_getwindowsizeinpixels(&w, &h);
    g_windowWidth = w;
    g_windowHeight = h;
    g_metalLayer.drawableSize = CGSizeMake((CGFloat)g_windowWidth, (CGFloat)g_windowHeight);

    /* Create default sampler */
    MTLSamplerDescriptor *sampDesc = [[MTLSamplerDescriptor alloc] init];
    sampDesc.minFilter = MTLSamplerMinMagFilterLinear;
    sampDesc.magFilter = MTLSamplerMinMagFilterLinear;
    sampDesc.sAddressMode = MTLSamplerAddressModeRepeat;
    sampDesc.tAddressMode = MTLSamplerAddressModeRepeat;
    g_defaultSampler = [g_device newSamplerStateWithDescriptor:sampDesc];
    [sampDesc release];
    
    /* Embedded 3D fallback shader (combined vertex + fragment in one MSL source).
     * Handles VERTEX_FORMAT_FULL: position(0), normal(1), tangent(2), bitangent(3), texcoord(4).
     * Model matrix at buffer(1) to avoid conflict with vertex attribute buffer(0). */
    const char *default3dCombined =
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "\n"
        "struct VertexIn {\n"
        "    float3 position [[attribute(0)]];\n"
        "    float3 normal   [[attribute(1)]];\n"
        "    float3 tangent  [[attribute(2)]];\n"
        "    float3 bitangent[[attribute(3)]];\n"
        "    float2 texcoord [[attribute(4)]];\n"
        "};\n"
        "\n"
        "struct VertexOut {\n"
        "    float4 position [[position]];\n"
        "    float2 texcoord;\n"
        "    float3 normal;\n"
        "};\n"
        "\n"
        "vertex VertexOut vertex_main(\n"
        "    VertexIn v [[stage_in]],\n"
        "    constant float4x4 &model [[buffer(1)]]\n"
        ") {\n"
        "    VertexOut out;\n"
        "    out.position = model * float4(v.position, 1.0);\n"
        "    out.texcoord = v.texcoord;\n"
        "    out.normal = v.normal;\n"
        "    return out;\n"
        "}\n"
        "\n"
        "fragment float4 fragment_main(\n"
        "    VertexOut in [[stage_in]]\n"
        ") {\n"
        "    /* Visualize normals as color for debugging */\n"
        "    return float4(in.normal * 0.5 + 0.5, 1.0);\n"
        "}\n";

    g_defaultVertShader = (MetalShader *)graphics_createshader(default3dCombined, strlen(default3dCombined), NULL, 0);
    g_defaultFragShader = (MetalShader *)graphics_createshader(default3dCombined, strlen(default3dCombined), NULL, 0);

    /* Embedded text shaders (combined vertex + fragment in one MSL source) */
    const char *textCombined =
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "\n"
        "struct TextVertexInput {\n"
        "    float3 position [[attribute(0)]];\n"
        "    float2 texcoord [[attribute(1)]];\n"
        "};\n"
        "\n"
        "struct TextVertexOutput {\n"
        "    float4 position [[position]];\n"
        "    float2 texcoord;\n"
        "};\n"
        "\n"
        "vertex TextVertexOutput vertex_main(\n"
        "    TextVertexInput v [[stage_in]]\n"
        ") {\n"
        "    TextVertexOutput out;\n"
        "    out.position = float4(v.position, 1.0);\n"
        "    out.texcoord = v.texcoord;\n"
        "    return out;\n"
        "}\n"
        "\n"
        "struct TextFragmentInput {\n"
        "    float2 texcoord;\n"
        "};\n"
        "\n"
        "fragment float4 fragment_main(\n"
        "    TextFragmentInput in [[stage_in]],\n"
        "    texture2d<float> tex [[texture(0)]],\n"
        "    sampler texSampler [[sampler(0)]]\n"
        ") {\n"
        "    float alpha = tex.sample(texSampler, in.texcoord).a;\n"
        "    return float4(1.0, 1.0, 1.0, alpha);\n"
        "}\n";

    g_textVertShader = (MetalShader *)graphics_createshader(textCombined, strlen(textCombined), NULL, 0);
    g_textFragShader = (MetalShader *)graphics_createshader(textCombined, strlen(textCombined), NULL, 0);
}

void graphics_get_text_shaders(Shader *out_vert, Shader *out_frag) {
    if (out_vert) *out_vert = (Shader)g_textVertShader;
    if (out_frag) *out_frag = (Shader)g_textFragShader;
}

void graphics_shutdown() {
    if (g_uniformBuffer) [g_uniformBuffer release];

    for (int i = 0; i < g_pipelineCount; i++) {
        MetalPipeline *mp = g_pipelines[i];
        if (mp->pipelineState) [mp->pipelineState release];
        if (mp->depthState) [mp->depthState release];
        free(mp);
    }
    free(g_pipelines);

    for (int i = 0; i < g_textureCount; i++) {
        MetalTexture *mt = g_textures[i];
        if (mt->texture) [mt->texture release];
        free(mt);
    }
    free(g_textures);

    for (int i = 0; i < g_bufferCount; i++) {
        MetalBuffer *mb = g_buffers[i];
        if (mb->buffer) [mb->buffer release];
        free(mb);
    }
    free(g_buffers);

    if (g_defaultVertShader) graphics_destroyshader((Shader)g_defaultVertShader);
    if (g_defaultFragShader) graphics_destroyshader((Shader)g_defaultFragShader);

    g_metalLayer = nil;

    if (g_commandQueue) [g_commandQueue release];

    printf("Metal graphics backend shut down\n");
}

void graphics_resize() {
    int w, h;
    window_getwindowsizeinpixels(&w, &h);
    g_windowWidth = w;
    g_windowHeight = h;
    if (g_metalLayer) {
        g_metalLayer.drawableSize = CGSizeMake((CGFloat)g_windowWidth, (CGFloat)g_windowHeight);
    }
    printf("Graphics resized to %dx%d\n", g_windowWidth, g_windowHeight);
}

int graphics_isminimized() {
    return g_minimized;
}

void graphics_predraw() {
    /* Get next drawable from Metal layer */
    g_currentDrawable = [g_metalLayer nextDrawable];
    if (!g_currentDrawable) {
        g_minimized = 1;
        return;
    }
    g_minimized = 0;

    /* Create render pass descriptor */
    MTLRenderPassDescriptor *passDesc = [MTLRenderPassDescriptor new];
    passDesc.colorAttachments[0].texture = g_currentDrawable.texture;
    passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    passDesc.colorAttachments[0].clearColor = MTLClearColorMake(
        CLEAR_COLOR[0], CLEAR_COLOR[1], CLEAR_COLOR[2], CLEAR_COLOR[3]);
    passDesc.depthAttachment.clearDepth = 1.0;

    /* Begin command buffer and render encoder */
    if (g_currentCommandBuffer) [g_currentCommandBuffer release];
    g_currentCommandBuffer = (id<MTLCommandBuffer>)metal_command_queue_new_command_buffer(g_commandQueue);

    g_currentEncoder = (id<MTLRenderCommandEncoder>)metal_command_buffer_new_render_encoder(g_currentCommandBuffer, passDesc);

    /* Set viewport */
    double vp[6] = {
        0.0, 0.0,
        (double)g_windowWidth, (double)g_windowHeight,
        0.0, 1.0
    };
    metal_encoder_set_viewport(g_currentEncoder, vp);

    /* Default depth state */
    MTLDepthStencilDescriptor *d = [[MTLDepthStencilDescriptor alloc] init];
    d.depthCompareFunction = MTLCompareFunctionLess;
    d.depthWriteEnabled = YES;
    metal_encoder_set_depth_stencil_state(g_currentEncoder, metal_device_new_depth_stencil_state(g_device, d));

    g_currentPipeline = NULL;
}

void graphics_postdraw() {
    if (g_currentEncoder) {
        metal_encoder_end_encoding(g_currentEncoder);
        g_currentEncoder = nil;
    }
}

void graphics_present() {
    if (!g_currentCommandBuffer || g_minimized) return;

    metal_command_buffer_present(g_currentCommandBuffer, g_currentDrawable);
    metal_command_buffer_commit(g_currentCommandBuffer);
    metal_command_buffer_wait(g_currentCommandBuffer);

    [g_currentCommandBuffer release];
    g_currentCommandBuffer = nil;
    g_currentDrawable = nil;
}

/* ------------------------------------------------------------------ */
/*  Model loading and drawing                                          */
/* ------------------------------------------------------------------ */

Model *graphics_loadmodel(const char *filepath) {
    Model *model = model_load(filepath);
    if (!model) return NULL;

    /* Create GPU vertex and index buffers for each mesh */
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
    if (!model || !g_currentEncoder) return;

    Shader vertShader = mat ? mat : (Shader)g_defaultVertShader;
    Shader fragShader = mat ? mat : (Shader)g_defaultFragShader;
    if (!vertShader || !fragShader) return;

    RasterState state = {.depthWrite = 1, .depthTest = 1, .backfaceCulling = 1, .blendMode = BLEND_NONE};
    Pipeline p = graphics_createpipeline(vertShader, fragShader, VERTEX_FORMAT_FULL, state);
    if (p) graphics_bindpipeline(p);

    /* Upload transform matrix to uniform buffer and bind at index 1 */
    if (transform4x4 && g_uniformBuffer) {
        memcpy(metal_buffer_get_contents(g_uniformBuffer), transform4x4, 64);
        metal_encoder_set_vertex_buffer(g_currentEncoder, g_uniformBuffer, 0, 1);
    }

    for (uint32_t i = 0; i < model->meshCount; i++) {
        Mesh *mesh = &model->meshes[i];

        if (mesh->vertexBuffer) {
            MetalBuffer *vb = (MetalBuffer *)mesh->vertexBuffer;
            metal_encoder_set_vertex_buffer(g_currentEncoder, vb->buffer, 0, 0);
        }

        if (mesh->indexBuffer && mesh->indexCount > 0) {
            MetalBuffer *ib = (MetalBuffer *)mesh->indexBuffer;
            metal_encoder_draw_indexed_primitives(g_currentEncoder, MTLPrimitiveTypeTriangle,
                                                  mesh->indexCount, MTLIndexTypeUInt32,
                                                  ib->buffer, 0, 1);
        } else if (mesh->vertexCount > 0) {
            metal_encoder_draw_primitives(g_currentEncoder, MTLPrimitiveTypeTriangle,
                                          0, mesh->vertexCount);
        }
    }
}

void graphics_draw_instanced(Model *model, Material mat, const float *transforms4x4, size_t count) {
    if (!model || !g_currentEncoder) return;

    Shader vertShader = mat ? mat : (Shader)g_defaultVertShader;
    Shader fragShader = mat ? mat : (Shader)g_defaultFragShader;
    if (!vertShader || !fragShader) return;

    RasterState state = {.depthWrite = 1, .depthTest = 1, .backfaceCulling = 1, .blendMode = BLEND_NONE};
    Pipeline p = graphics_createpipeline(vertShader, fragShader, VERTEX_FORMAT_FULL, state);
    if (p) graphics_bindpipeline(p);

    /* Upload first transform to uniform buffer and bind at index 1 */
    if (transforms4x4 && g_uniformBuffer) {
        memcpy(metal_buffer_get_contents(g_uniformBuffer), transforms4x4, 64);
        metal_encoder_set_vertex_buffer(g_currentEncoder, g_uniformBuffer, 0, 1);
    }

    for (uint32_t i = 0; i < model->meshCount; i++) {
        Mesh *mesh = &model->meshes[i];

        if (mesh->vertexBuffer) {
            MetalBuffer *vb = (MetalBuffer *)mesh->vertexBuffer;
            metal_encoder_set_vertex_buffer(g_currentEncoder, vb->buffer, 0, 0);
        }

        if (mesh->indexBuffer && mesh->indexCount > 0) {
            MetalBuffer *ib = (MetalBuffer *)mesh->indexBuffer;
            metal_encoder_draw_indexed_primitives(g_currentEncoder, MTLPrimitiveTypeTriangle,
                                                  mesh->indexCount, MTLIndexTypeUInt32,
                                                  ib->buffer, 0, (unsigned long)count);
        }
    }
}

void graphics_draw_buffers(Buffer vertexBuffer, Buffer indexBuffer, size_t indexCount,
                           Material mat, const float *transform4x4) {
    (void)mat;
    if (!g_currentEncoder || !vertexBuffer) return;

    MetalBuffer *vb = (MetalBuffer *)vertexBuffer;
    metal_encoder_set_vertex_buffer(g_currentEncoder, vb->buffer, 0, 0);

    /* Upload transform matrix to uniform buffer and bind at index 1 */
    if (transform4x4 && g_uniformBuffer) {
        memcpy(metal_buffer_get_contents(g_uniformBuffer), transform4x4, 64);
        metal_encoder_set_vertex_buffer(g_currentEncoder, g_uniformBuffer, 0, 1);
    }

    if (indexBuffer && indexCount > 0) {
        MetalBuffer *ib = (MetalBuffer *)indexBuffer;
        metal_encoder_draw_indexed_primitives(g_currentEncoder, MTLPrimitiveTypeTriangle,
                                              (NSUInteger)indexCount, MTLIndexTypeUInt32,
                                              ib->buffer, 0, 1);
    }
}
