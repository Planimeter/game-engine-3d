/* Copyright Planimeter. All Rights Reserved. */

#ifndef CAMERA_H
#define CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

/* Fly camera: position + yaw/pitch orientation, produces view and
 * projection matrices on top of the math_c API. */
typedef struct {
    float position[3];
    float yaw;    /* radians; 0 looks down -Z, positive turns right */
    float pitch;  /* radians; clamped to ~+-89 degrees */
    float fovY;   /* radians */
    float zNear;
    float zFar;
} Camera;

/* Place the camera at (x, y, z) with default orientation and clipping. */
void camera_init(Camera *cam, float x, float y, float z);

/* Unit direction vectors: forward, right (screen +X), and camera up. */
void camera_basis(const Camera *cam, float fwd[3], float right[3], float up[3]);

/* View matrix (world -> camera space). */
void camera_view(const Camera *cam, float *out16);

/* Right-handed perspective projection matrix (GL depth, 0..1). */
void camera_projection(const Camera *cam, float aspect, float *out16);

/* Reverse-Z projection with an infinite far plane. Depth is 1/z, so the
 * backends must compare with GREATER and clear depth to 0. */
void camera_projection_reversez(const Camera *cam, float aspect, float *out16);

/* Apply mouse-look from relative motion (pixels, right/down positive).
 * sensitivity is radians per pixel (e.g. 0.0025f). */
void camera_look(Camera *cam, float dx, float dy, float sensitivity);

/* Move along the camera basis: forward/right/up are -1..1 strides,
 * distance is the world-space length of one full stride. */
void camera_move(Camera *cam, float forward, float right, float up, float distance);

/* Adjust field of view from a mouse wheel notch (positive = zoom in). */
void camera_zoom(Camera *cam, float wheelY);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_H */
