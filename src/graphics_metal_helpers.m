/* Copyright Planimeter. All Rights Reserved.
 * Pure Objective-C implementation of Metal helper functions.
 * This file must be compiled as .m (not .mm) so Metal protocol
 * methods are correctly resolved by the ObjC compiler.
 *
 * Note: unsigned long parameters are cast to NSUInteger internally.
 * NSUInteger == unsigned long on all Apple platforms, so this is safe.
 */

#import "graphics_metal_helpers.h"
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

void  *metal_texture_get_contents(void *tex) {
    return [(id<MTLTexture>)tex contents];
}

void   metal_texture_update_region(void *tex,
                                    unsigned long x,
                                    unsigned long y,
                                    unsigned long width,
                                    unsigned long height,
                                    const void *src,
                                    unsigned long bytesPerRow) {
    /* Use staging buffer + blit encoder for maximum compatibility.
     * AGX (Apple GPU) driver does not respond to replaceBytesInRegion: or
     * contents on textures in certain configurations. */
    id<MTLTexture> texture = (id<MTLTexture>)tex;
    id<MTLDevice> dev = texture.device;
    
    NSUInteger totalBytes = (NSUInteger)(bytesPerRow * height);
    id<MTLBuffer> staging = [dev newBufferWithBytes:src
                                             length:totalBytes
                                            options:MTLResourceStorageModeShared];
    
    id<MTLCommandBuffer> cb = [[dev newCommandQueue] commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    
    MTLRegion region = MTLRegionMake2D((NSUInteger)x, (NSUInteger)y,
                                        (NSUInteger)width, (NSUInteger)height);
    [blit copyFromBuffer:staging
            sourceOffset:0
       sourceBytesPerRow:(NSUInteger)bytesPerRow
     sourceBytesPerImage:totalBytes
              sourceSize:MTLSizeMake((NSUInteger)width, (NSUInteger)height, 1)
               toTexture:texture
        destinationSlice:0
        destinationLevel:0
       destinationOrigin:region.origin];
    
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    
    [staging release];
    [cb release];
}

void  *metal_buffer_get_contents(void *buf) {
    return [(id<MTLBuffer>)buf contents];
}

void   metal_buffer_update(void *buf, const void *data, size_t size) {
    memcpy([(id<MTLBuffer>)buf contents], data, size);
}

void   metal_encoder_set_vertex_buffer(void *enc,
                                        void *buf,
                                        unsigned long offset,
                                        unsigned long index) {
    [(id<MTLRenderCommandEncoder>)enc setVertexBuffer:(id<MTLBuffer>)buf
                                               offset:(NSUInteger)offset
                                              atIndex:(NSUInteger)index];
}

void   metal_encoder_set_buffer(void *enc,
                                 void *buf,
                                 unsigned long offset,
                                 unsigned long index) {
    [(id<MTLRenderCommandEncoder>)enc setBuffer:(id<MTLBuffer>)buf
                                         offset:(NSUInteger)offset
                                        atIndex:(NSUInteger)index];
}

void   metal_encoder_set_viewport(void *enc, const double vp[6]) {
    MTLViewport mtlVp;
    mtlVp.originX = vp[0];
    mtlVp.originY = vp[1];
    mtlVp.width   = vp[2];
    mtlVp.height  = vp[3];
    mtlVp.znear   = vp[4];
    mtlVp.zfar    = vp[5];
    [(id<MTLRenderCommandEncoder>)enc setViewport:mtlVp];
}

void   metal_encoder_set_render_pipeline_state(void *enc,
                                                void *pso) {
    [(id<MTLRenderCommandEncoder>)enc setRenderPipelineState:(id<MTLRenderPipelineState>)pso];
}

void   metal_encoder_set_depth_stencil_state(void *enc,
                                              void *ds) {
    [(id<MTLRenderCommandEncoder>)enc setDepthStencilState:(id<MTLDepthStencilState>)ds];
}

void   metal_encoder_set_fragment_texture(void *enc,
                                           void *tex,
                                           unsigned long slot) {
    [(id<MTLRenderCommandEncoder>)enc setFragmentTexture:(id<MTLTexture>)tex
                                                  atIndex:(NSUInteger)slot];
}

void   metal_encoder_set_fragment_sampler_state(void *enc,
                                                 void *ss,
                                                 unsigned long slot) {
    [(id<MTLRenderCommandEncoder>)enc setFragmentSamplerState:(id<MTLSamplerState>)ss
                                                       atIndex:(NSUInteger)slot];
}

void   metal_encoder_end_encoding(void *enc) {
    [(id<MTLRenderCommandEncoder>)enc endEncoding];
}

void   metal_encoder_draw_primitives(void *enc,
                                      int type,
                                      unsigned long start,
                                      unsigned long count) {
    [(id<MTLRenderCommandEncoder>)enc drawPrimitives:(MTLPrimitiveType)type
                                         vertexStart:(NSUInteger)start
                                         vertexCount:(NSUInteger)count];
}

void   metal_encoder_draw_indexed_primitives(void *enc,
                                              int type,
                                              unsigned long indexCount,
                                              int indexType,
                                              void *indexBuffer,
                                              unsigned long indexBufferOffset,
                                              unsigned long instanceCount) {
    [(id<MTLRenderCommandEncoder>)enc drawIndexedPrimitives:(MTLPrimitiveType)type
                                                 indexCount:(NSUInteger)indexCount
                                                  indexType:(MTLIndexType)indexType
                                                indexBuffer:(id<MTLBuffer>)indexBuffer
                                          indexBufferOffset:(NSUInteger)indexBufferOffset
                                              instanceCount:(NSUInteger)instanceCount];
}

void  *metal_device_new_buffer(void *dev, unsigned long len, unsigned long opt) {
    return [(id<MTLDevice>)dev newBufferWithLength:(NSUInteger)len
                                           options:(MTLResourceOptions)opt];
}

void  *metal_device_new_texture(void *dev, void *desc) {
    return [(id<MTLDevice>)dev newTextureWithDescriptor:(MTLTextureDescriptor *)desc];
}

void  *metal_device_new_library(void *dev, const char *source, void **err) {
    NSString *srcStr = [NSString stringWithUTF8String:source];
    id<MTLLibrary> lib = [(id<MTLDevice>)dev newLibraryWithSource:srcStr
                                                          options:nil
                                                            error:(NSError **)err];
    return (__bridge_retained void *)lib;
}

void  *metal_device_new_command_queue(void *dev) {
    return [(id<MTLDevice>)dev newCommandQueue];
}

void  *metal_library_new_function(void *lib, const char *name) {
    NSString *nameStr = [NSString stringWithUTF8String:name];
    return [(id<MTLLibrary>)lib newFunctionWithName:nameStr];
}

void  *metal_device_new_pipeline_state(void *dev, void *desc, void **err) {
    return [(id<MTLDevice>)dev newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)desc
                                                              error:(NSError **)err];
}

void  *metal_device_new_depth_stencil_state(void *dev, void *desc) {
    return [(id<MTLDevice>)dev newDepthStencilStateWithDescriptor:(MTLDepthStencilDescriptor *)desc];
}

void  *metal_command_queue_new_command_buffer(void *q) {
    return [(id<MTLCommandQueue>)q commandBuffer];
}

void   metal_command_buffer_present(void *cb, void *d) {
    [(id<MTLCommandBuffer>)cb presentDrawable:(id<CAMetalDrawable>)d];
}

void   metal_command_buffer_commit(void *cb) {
    [(id<MTLCommandBuffer>)cb commit];
}

void   metal_command_buffer_wait(void *cb) {
    [(id<MTLCommandBuffer>)cb waitUntilCompleted];
}

void  *metal_command_buffer_new_render_encoder(void *cb, void *desc) {
    return [(id<MTLCommandBuffer>)cb renderCommandEncoderWithDescriptor:(MTLRenderPassDescriptor *)desc];
}
