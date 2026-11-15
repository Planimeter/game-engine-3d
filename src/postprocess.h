/* Copyright Planimeter. All Rights Reserved. */

#ifndef POSTPROCESS_H
#define POSTPROCESS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Photographic post-process chain.
 *
 * The frame is rendered into an offscreen RGBA16F scene target instead of
 * straight to the swapchain, then composited by a fullscreen pass. The 16-bit
 * float target is the point: effects downstream of the scene see values above
 * 1.0 rather than an already-clipped 8-bit image, which is what a lens effect
 * needs to roll off instead of flattening.
 *
 * The chain is optional. Where the backend cannot render to an offscreen
 * colour target (Vulkan, today), init does nothing and begin/end are no-ops,
 * so the frame is drawn directly to the swapchain exactly as before.
 *
 * Usage, once per frame, around the 3D scene and outside the 2D overlay:
 *
 *     postprocess_begin();
 *     ...3D draws...
 *     postprocess_end();
 *     ...overlay draws...
 */
void postprocess_init(void);
void postprocess_shutdown(void);

/* Open the offscreen scene pass. Call before the 3D draws. */
void postprocess_begin(void);

/* Close the scene pass, composite it into the swapchain and resume the
 * swapchain pass, which the overlay then draws into. */
void postprocess_end(void);

#ifdef __cplusplus
}
#endif

#endif /* POSTPROCESS_H */
