/* Copyright Planimeter. All Rights Reserved. */

#include "camera.h"
#include "math_c.h"
#include <math.h>

#define CAMERA_PITCH_LIMIT 1.55334f /* ~89 degrees */
#define CAMERA_FOV_MIN     0.35f    /* ~20 degrees */
#define CAMERA_FOV_MAX     1.75f    /* ~100 degrees */

void camera_init(Camera *cam, float x, float y, float z)
{
    if (!cam) return;
    cam->position[0] = x;
    cam->position[1] = y;
    cam->position[2] = z;
    cam->yaw = 0.0f;
    cam->pitch = -0.15f;
    cam->fovY = 3.14159265f * 0.5f;
    cam->zNear = 0.1f;
    cam->zFar = 100.0f;
}

void camera_basis(const Camera *cam, float fwd[3], float right[3], float up[3])
{
    if (!cam) return;

    float cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    float cy = cosf(cam->yaw), sy = sinf(cam->yaw);

    if (fwd) {
        fwd[0] = cp * sy;
        fwd[1] = sp;
        fwd[2] = -cp * cy;
    }
    if (right) {
        right[0] = cy;
        right[1] = 0.0f;
        right[2] = sy;
    }
    if (up) {
        up[0] = -sy * sp;
        up[1] = cp;
        up[2] = cy * sp;
    }
}

void camera_view(const Camera *cam, float *out16)
{
    if (!cam || !out16) return;

    float fwd[3], up[3];
    camera_basis(cam, fwd, NULL, up);

    math_lookat(out16,
                cam->position[0],
                cam->position[1],
                cam->position[2],
                cam->position[0] + fwd[0],
                cam->position[1] + fwd[1],
                cam->position[2] + fwd[2],
                up[0], up[1], up[2]);
}

void camera_projection(const Camera *cam, float aspect, float *out16)
{
    if (!cam || !out16) return;
    math_perspective(out16, cam->fovY, aspect, cam->zNear, cam->zFar);
}

void camera_look(Camera *cam, float dx, float dy, float sensitivity)
{
    if (!cam) return;

    cam->yaw += dx * sensitivity;
    cam->pitch -= dy * sensitivity;

    if (cam->pitch > CAMERA_PITCH_LIMIT) cam->pitch = CAMERA_PITCH_LIMIT;
    if (cam->pitch < -CAMERA_PITCH_LIMIT) cam->pitch = -CAMERA_PITCH_LIMIT;

    /* Keep yaw in [-pi, pi] to avoid drift over long sessions */
    if (cam->yaw > 3.14159265f) cam->yaw -= 3.14159265f * 2.0f;
    if (cam->yaw < -3.14159265f) cam->yaw += 3.14159265f * 2.0f;
}

void camera_move(Camera *cam, float forward, float right, float up, float distance)
{
    if (!cam) return;

    float fwd[3], rgt[3], upv[3];
    camera_basis(cam, fwd, rgt, upv);

    float f = forward * distance;
    float r = right * distance;
    float u = up * distance;

    cam->position[0] += fwd[0] * f + rgt[0] * r + upv[0] * u;
    cam->position[1] += fwd[1] * f + rgt[1] * r + upv[1] * u;
    cam->position[2] += fwd[2] * f + rgt[2] * r + upv[2] * u;
}

void camera_zoom(Camera *cam, float wheelY)
{
    if (!cam) return;

    cam->fovY -= wheelY * 0.06f;
    if (cam->fovY < CAMERA_FOV_MIN) cam->fovY = CAMERA_FOV_MIN;
    if (cam->fovY > CAMERA_FOV_MAX) cam->fovY = CAMERA_FOV_MAX;
}
