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
typedef void *Pipeline;

typedef enum {
	SHADER_STAGE_VERTEX,
	SHADER_STAGE_FRAGMENT
} ShaderStage;

typedef enum {
	BLEND_NONE,
	BLEND_ALPHA,
	BLEND_ADD,
	BLEND_PREMULT
} BlendMode;

/* Pixel format of a colour render target. The swapchain is an 8-bit unorm
 * target, which COLOR_FORMAT_RGBA8 names; COLOR_FORMAT_RGBA16F is the
 * offscreen scene target, so effects downstream of the scene have headroom
 * above 1.0 instead of a clipped 8-bit image.
 *
 * COLOR_FORMAT_RGBA32F stores shader output without rounding at all. It is
 * only useful to check that a compositing pass is exact, since 16-bit float
 * carries less precision than a shader computes with: see postprocess.c.
 *
 * A pipeline is built for one colour format and cannot be used in a pass of
 * another, so graphics_createpipeline() keys its cache on it. */
typedef enum {
	COLOR_FORMAT_RGBA8,    // 8-bit swapchain-class target
	COLOR_FORMAT_RGBA16F,  // 16-bit float offscreen target
	COLOR_FORMAT_RGBA32F   // 32-bit float, for checking a pass is lossless
} ColorFormat;

typedef enum {
	VERTEX_FORMAT_FULL,     // Position, Normal, UV, Tangent, Bitangent
	VERTEX_FORMAT_SKINNED,  // Position, Normal, UV, Tangent, Bitangent, BoneIDs, BoneWeights
	VERTEX_FORMAT_POS_UV,   // Position, UV only (for text/UI)
	VERTEX_FORMAT_POS_COLOR,// Position, Color only (for debug)
	VERTEX_FORMAT_INSTANCE  // VERTEX_FORMAT_FULL vertices; per-instance mat4s are
	                         // read by instance_id, so no extra vertex attributes
} VertexFormat;

typedef struct {
	int depthWrite;
	int depthTest;
	int backfaceCulling;
	BlendMode blendMode;
	/* Target format this pipeline is built for. Zero (COLOR_FORMAT_RGBA8)
	 * is the default, so existing initialisers keep working. Draw calls the
	 * engine issues itself take the format from the pass being recorded. */
	ColorFormat colorFormat;
} RasterState;

void        graphics_init();
void        graphics_shutdown();
void        graphics_resize();
int         graphics_isminimized();
void        graphics_predraw();
void        graphics_postdraw();
void        graphics_present();

void        graphics_setshader(Shader vertShader, Shader fragShader);

Shader      graphics_createshader(ShaderStage stage,
                                  const char *source, size_t size,
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

/* Build a Model directly from CPU vertex/index data, for geometry
 * generated at runtime (terrain) rather than loaded from a file. The data
 * is copied, so the caller keeps ownership of the arrays. vertexStride is
 * the byte size of one engine Vertex. */
Model      *graphics_createmesh(const void *vertices, size_t vertexCount,
                               size_t vertexStride,
                               const uint32_t *indices, size_t indexCount);
void        graphics_destroymodel(Model *model);

/* Bind every mesh texture the model owns and tell the shader which
 * bindless slot to sample. Safe to call for untextured models (passes -1).
 * Call before graphics_drawmodel(), since the slot goes in the material's
 * float block. */
void        graphics_bindmodeltexture(Model *model, Material mat);

/* Re-upload a model's vertices after the CPU-side mesh data changed (e.g.
 * model_normalize), keeping the existing GPU buffer allocations. */
void        graphics_reuploadmodel(Model *model);

/* Skinning. Upload bone matrices (boneCount * 16 floats, from model_pose)
 * for the next draw. Passing boneCount 0 disables skinning, which is what
 * static models want: their vertices all carry bone 0 at weight 1, so the
 * shader collapses to the identity. The upload is per draw, so a frame with
 * several skinned models must call this before each one. */
void        graphics_setbones(Model *model, const float *boneMatrices, uint32_t boneCount);

/* Bind an identity skin matrix set. Vertex shaders read bone matrices
 * unconditionally, so this must be called for draws that have none, or the
 * skinning buffer is unbound and all geometry collapses to the origin. */
void        graphics_bindskinidentity(void);
void        graphics_drawmodel(Model *model,
                               Material mat,
                               const float *transform4x4);
/* Draw `count` copies of `model` at the per-instance matrices in
 * transforms4x4 (16 floats each), transformed by viewProjection4x4.
 * Metal issues a single instanced draw; the Vulkan backend currently loops
 * ordinary draws, so treat the instance count as a hint, not a guarantee. */
void        graphics_draw_instanced(Model *model,
                                    Material mat,
                                    const float *transforms4x4,
                                    const float *viewProjection4x4,
                                    size_t count);
void        graphics_draw_buffers(Buffer vertexBuffer,
                                  Buffer indexBuffer,
                                  size_t indexCount,
                                  size_t firstIndex,
                                  Material mat,
                                  const float *transform4x4);

/* Draw a fullscreen triangle that samples `source` at slot 1 through the
 * given shader pair, into whatever target is currently being recorded.
 * Geometry is generated by the backend, so no vertex data is needed, and the
 * source is sampled with a clamp-to-edge sampler since an effect can read
 * outside the frame. `mat` supplies uniforms at slot 2 and may be NULL. */
void        graphics_draw_fullscreen(Texture source,
                                     Shader vertShader,
                                     Shader fragShader,
                                     Material mat);

Buffer      graphics_createvertexbuffer(const void *data, size_t size);
Buffer      graphics_createindexbuffer(const void *data, size_t size);
Buffer      graphics_createuniformbuffer(size_t size);
void        graphics_updatebuffer(Buffer buf, const void *data, size_t size);
void        graphics_binduniformbuffer(Buffer buf, unsigned slot);
void        graphics_destroybuffer(Buffer buf);

Texture     graphics_createtexture(Texture src);
Texture     graphics_createtexture_rgba(int width,
                                        int height,
                                        const unsigned char *pixels);
/* Create an empty texture usable as a render target, for offscreen passes.
 * Returns NULL when the backend cannot render to an offscreen colour target
 * (the Vulkan backend today), so callers can fall back to drawing straight
 * to the swapchain. */
Texture     graphics_createtexture_target(int width, int height,
                                          ColorFormat format);
void        graphics_updatetexture(Texture tex,
                                   int x, int y,
                                   int width, int height,
                                   const unsigned char *pixels);
void        graphics_destroytexture(Texture tex);
void        graphics_bindtexture(Texture tex, unsigned slot);

RenderPass  graphics_createpass(const char *name, RasterState state);
void        graphics_pass_set_color_texture(RenderPass pass, Texture tex, unsigned slot);
void        graphics_pass_set_depth_texture(RenderPass pass, Texture tex);
void        graphics_beginpass(RenderPass pass);
void        graphics_endpass(RenderPass pass);
void        graphics_destroypass(RenderPass pass);

Pipeline    graphics_createpipeline(Shader vertShader,
                                    Shader fragShader,
                                    VertexFormat format,
                                    RasterState state);
void        graphics_bindpipeline(Pipeline pipeline);
void        graphics_destroypipeline(Pipeline pipeline);

Shader      graphics_get_shader_variant(Shader base,
                                        const char **defines,
                                        size_t defineCount);

/* GAME_CAPTURE: arm a readback of the next presented frame, then collect it.
 * graphics_capture_setup() is called before the draw of the frame to capture;
 * graphics_capture_read() after graphics_present() returns an RGBA buffer the
 * caller frees, or NULL when the backend cannot read its drawable back. */
void        graphics_capture_setup(void);
unsigned char *graphics_capture_read(int *width, int *height);

/* Metal backend: returns embedded MSL text shaders when SPIR-V unavailable */
void        graphics_get_text_shaders(Shader *out_vert, Shader *out_frag);

#ifdef __cplusplus
}
#endif

#endif /* GRAPHICS_H */
