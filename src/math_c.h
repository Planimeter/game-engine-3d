/* Copyright Planimeter. All Rights Reserved. */

#ifndef MATH_C_H
#define MATH_C_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sets a 4x4 column-major matrix to identity.
 *
 * @param m 16-element float array (column-major) to set to identity.
 */
void math_identity(float *m);

/**
 * Multiplies two 4x4 column-major matrices: out = a * b.
 *
 * Safe if out overlaps a or b (uses a temporary internally).
 *
 * @param out 16-element float array receiving the product.
 * @param a    16-element float array, left operand.
 * @param b    16-element float array, right operand.
 */
void math_multiply(float *out, const float *a, const float *b);

/**
 * Builds a rotation matrix from an axis and angle, then multiplies:
 * out = m * rotate(angle, axis).
 *
 * @param out   16-element float array receiving the result.
 * @param m     16-element float array, input transform.
 * @param angle Rotation angle in radians.
 * @param x     X component of the rotation axis.
 * @param y     Y component of the rotation axis.
 * @param z     Z component of the rotation axis.
 */
void math_rotate(float *out, const float *m, float angle, float x, float y, float z);

/**
 * Builds a translation matrix and multiplies: out = m * translate(x, y, z).
 *
 * @param out 16-element float array receiving the result.
 * @param m   16-element float array, input transform.
 * @param x   Translation along X.
 * @param y   Translation along Y.
 * @param z   Translation along Z.
 */
void math_translate(float *out, const float *m, float x, float y, float z);

/**
 * Builds a scale matrix and multiplies: out = m * scale(x, y, z).
 *
 * @param out 16-element float array receiving the result.
 * @param m   16-element float array, input transform.
 * @param x   Scale factor along X.
 * @param y   Scale factor along Y.
 * @param z   Scale factor along Z.
 */
void math_scale(float *out, const float *m, float x, float y, float z);

/**
 * Creates a right-handed perspective projection matrix.
 *
 * @param m      16-element float array receiving the projection matrix.
 * @param fov_y  Vertical field of view in radians.
 * @param aspect Aspect ratio (width / height).
 * @param zNear  Near clipping plane distance.
 * @param zFar   Far clipping plane distance.
 */
void math_perspective(float *m, float fov_y, float aspect, float zNear, float zFar);

/**
 * Creates a right-handed look-at view matrix.
 *
 * @param m        16-element float array receiving the view matrix.
 * @param eyeX     X position of the camera.
 * @param eyeY     Y position of the camera.
 * @param eyeZ     Z position of the camera.
 * @param centerX  X position of the target.
 * @param centerY  Y position of the target.
 * @param centerZ  Z position of the target.
 * @param upX      X component of the up vector.
 * @param upY      Y component of the up vector.
 * @param upZ      Z component of the up vector.
 */
void math_lookat(float *m,
                 float eyeX, float eyeY, float eyeZ,
                 float centerX, float centerY, float centerZ,
                 float upX, float upY, float upZ);

/**
 * Creates an orthographic projection matrix.
 *
 * @param m      16-element float array receiving the projection matrix.
 * @param left   Left clipping plane.
 * @param right  Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top    Top clipping plane.
 * @param zNear  Near clipping plane distance.
 * @param zFar   Far clipping plane distance.
 */
void math_ortho(float *m, float left, float right, float bottom, float top, float zNear, float zFar);

#ifdef __cplusplus
}
#endif

#endif /* MATH_C_H */