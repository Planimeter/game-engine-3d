/* Copyright Planimeter. All Rights Reserved. */

#include "math_c.h"
#include <string.h>

void math_identity(float *m)
{
    if (!m) return;
    for (int i = 0; i < 16; i++) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void math_multiply(float *out, const float *a, const float *b)
{
    if (!out || !a || !b) return;
    float t[16];
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            t[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    memcpy(out, t, 16 * sizeof(float));
}

void math_rotate(float *out, const float *m, float angle, float x, float y, float z)
{
    (void)out;
    (void)m;
    (void)angle;
    (void)x;
    (void)y;
    (void)z;
}

void math_translate(float *out, const float *m, float x, float y, float z)
{
    (void)out;
    (void)m;
    (void)x;
    (void)y;
    (void)z;
}

void math_scale(float *out, const float *m, float x, float y, float z)
{
    (void)out;
    (void)m;
    (void)x;
    (void)y;
    (void)z;
}

void math_perspective(float *m, float fov_y, float aspect, float zNear, float zFar)
{
    if (!m) return;
    for (int i = 0; i < 16; i++) m[i] = 0.0f;
    float f = 1.0f / tanf(fov_y * 0.5f);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zFar + zNear) / (zNear - zFar);
    m[11] = -1.0f;
    m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    m[15] = 0.0f;
}

void math_lookat(float *m,
                 float eyeX, float eyeY, float eyeZ,
                 float centerX, float centerY, float centerZ,
                 float upX, float upY, float upZ)
{
    if (!m) return;
    float fx = centerX - eyeX, fy = centerY - eyeY, fz = centerZ - eyeZ;
    float fl = sqrtf(fx*fx + fy*fy + fz*fz);
    if (fl > 0.0f) { fx /= fl; fy /= fl; fz /= fl; }
    float sx = fy * upZ - fz * upY;
    float sy = fz * upX - fx * upZ;
    float sz = fx * upY - fy * upX;
    float sl = sqrtf(sx*sx + sy*sy + sz*sz);
    if (sl > 0.0f) { sx /= sl; sy /= sl; sz /= sl; }
    float ux = sy * fz - sz * fy;
    float uy = sz * fx - sx * fz;
    float uz = sx * fy - sy * fx;
    m[0] = sx; m[1] = ux; m[2] = -fx; m[3] = 0.0f;
    m[4] = sy; m[5] = uy; m[6] = -fy; m[7] = 0.0f;
    m[8] = sz; m[9] = uz; m[10] = -fz; m[11] = 0.0f;
    m[12] = -(sx*eyeX + sy*eyeY + sz*eyeZ);
    m[13] = -(ux*eyeX + uy*eyeY + uz*eyeZ);
    m[14] =  fx*eyeX + fy*eyeY + fz*eyeZ;
    m[15] = 1.0f;
}

void math_ortho(float *m, float left, float right, float bottom, float top, float zNear, float zFar)
{
    (void)m;
    (void)left;
    (void)right;
    (void)bottom;
    (void)top;
    (void)zNear;
    (void)zFar;
}