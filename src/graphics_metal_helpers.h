/* Copyright Planimeter. All Rights Reserved.
 * Pure C-compatible header for Metal helper function declarations.
 * Uses opaque void* pointers so all symbols get extern "C" linkage,
 * avoiding linker mismatches between .mm (ObjC++) and .m (pure ObjC) units.
 *
 * All ObjC-specific types (NSUInteger, NSError, MTLViewport, etc.) are
 * mapped to plain C types here so this header can be included from both
 * .m (pure ObjC) and .mm (ObjC++) translation units without requiring
 * Foundation/Metal headers to be imported first.
 */

#ifndef GRAPHICS_METAL_HELPERS_H
#define GRAPHICS_METAL_HELPERS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque Metal object handles — cast to proper types inside .m file */
typedef struct objc_object MTLBuffer;
typedef struct objc_object MTLTexture;
typedef struct objc_object MTLLibrary;
typedef struct objc_object MTLFunction;
typedef struct objc_object MTLRenderPipelineState;
typedef struct objc_object MTLDepthStencilState;
typedef struct objc_object MTLCommandQueue;
typedef struct objc_object MTLCommandBuffer;
typedef struct objc_object MTLRenderCommandEncoder;
typedef struct objc_object CAMetalDrawable;

/* Metal texture helpers */
void  *metal_texture_get_contents(void *tex);
void   metal_texture_update_region(void *tex, unsigned long x, unsigned long y, unsigned long width, unsigned long height, const void *src, unsigned long bytesPerRow);

/* Metal buffer helpers */
void  *metal_buffer_get_contents(void *buf);
void   metal_buffer_update(void *buf, const void *data, size_t size);

/* Metal encoder helpers */
void   metal_encoder_set_vertex_buffer(void *enc, void *buf, unsigned long offset, unsigned long index);
void   metal_encoder_set_buffer(void *enc, void *buf, unsigned long offset, unsigned long index);
/* Viewport is passed as 6 doubles: originX, originY, width, height, znear, zfar */
void   metal_encoder_set_viewport(void *enc, const double vp[6]);
void   metal_encoder_set_render_pipeline_state(void *enc, void *pso);
void   metal_encoder_set_depth_stencil_state(void *enc, void *ds);
void   metal_encoder_set_fragment_texture(void *enc, void *tex, unsigned long slot);
void   metal_encoder_set_fragment_sampler_state(void *enc, void *ss, unsigned long slot);
void   metal_encoder_end_encoding(void *enc);
void   metal_encoder_draw_primitives(void *enc, int type, unsigned long start, unsigned long count);
void   metal_encoder_draw_indexed_primitives(void *enc, int type, unsigned long indexCount, int indexType, void *indexBuffer, unsigned long indexBufferOffset, unsigned long instanceCount);

/* Metal device helpers */
void  *metal_device_new_buffer(void *dev, unsigned long len, unsigned long opt);
void  *metal_device_new_texture(void *dev, void *desc);
void  *metal_device_new_library(void *dev, const char *source, void **err);
void  *metal_device_new_command_queue(void *dev);
void  *metal_library_new_function(void *lib, const char *name);
void  *metal_device_new_pipeline_state(void *dev, void *desc, void **err);
void  *metal_device_new_depth_stencil_state(void *dev, void *desc);

/* Metal command buffer helpers */
void  *metal_command_queue_new_command_buffer(void *q);
void   metal_command_buffer_present(void *cb, void *d);
void   metal_command_buffer_commit(void *cb);
void   metal_command_buffer_wait(void *cb);
void  *metal_command_buffer_new_render_encoder(void *cb, void *desc);

#ifdef __cplusplus
}
#endif

#endif /* GRAPHICS_METAL_HELPERS_H */
