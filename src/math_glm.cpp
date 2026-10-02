/* Copyright Planimeter. All Rights Reserved. */

#include "math_c.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <cstring>

void math_identity(float *m)
{
    glm::mat4 result(1.0f);
    std::memcpy(m, glm::value_ptr(result), 16 * sizeof(float));
}

void math_multiply(float *out, const float *a, const float *b)
{
    glm::mat4 ma = glm::make_mat4(a);
    glm::mat4 mb = glm::make_mat4(b);
    glm::mat4 result = ma * mb;
    std::memcpy(out, glm::value_ptr(result), 16 * sizeof(float));
}

void math_rotate(float *out, const float *m, float angle, float x, float y, float z)
{
    glm::mat4 mat = glm::make_mat4(m);
    glm::mat4 result = glm::rotate(mat, angle, glm::vec3(x, y, z));
    std::memcpy(out, glm::value_ptr(result), 16 * sizeof(float));
}

void math_translate(float *out, const float *m, float x, float y, float z)
{
    glm::mat4 mat = glm::make_mat4(m);
    glm::mat4 result = glm::translate(mat, glm::vec3(x, y, z));
    std::memcpy(out, glm::value_ptr(result), 16 * sizeof(float));
}

void math_scale(float *out, const float *m, float x, float y, float z)
{
    glm::mat4 mat = glm::make_mat4(m);
    glm::mat4 result = glm::scale(mat, glm::vec3(x, y, z));
    std::memcpy(out, glm::value_ptr(result), 16 * sizeof(float));
}

void math_perspective(float *m, float fov_y, float aspect, float zNear, float zFar)
{
    glm::mat4 result = glm::perspective(fov_y, aspect, zNear, zFar);
    std::memcpy(m, glm::value_ptr(result), 16 * sizeof(float));
}

/* Reverse-Z infinite-far projection.
 *
 * Depth is 1/z, so precision concentrates near the camera where it is
 * needed, and the far plane is pushed to infinity — no log math in the
 * shader, so early-Z still works. Requires the depth compare to be
 * GREATER and the clear value to be 0.
 *
 * The infinite far plane uses w = z for the far row instead of the usual
 * (far+near)/(near-far), so depth approaches 0 rather than 1 at infinity.
 */
void math_perspective_reversez(float *m, float fov_y, float aspect, float zNear)
{
    float tanHalf = tanf(fov_y * 0.5f);

    glm::mat4 result(0.0f);
    result[0][0] = 1.0f / (aspect * tanHalf);
    result[1][1] = 1.0f / tanHalf;
    result[2][2] = 0.0f;                    /* no z scaling: linear in z/w */
    result[2][3] = -1.0f;                    /* w = -z_eye */
    result[3][2] = zNear;                    /* z_ndc = zNear / |z_eye| */
    result[3][3] = 0.0f;

    std::memcpy(m, glm::value_ptr(result), 16 * sizeof(float));
}

void math_lookat(float *m,
                 float eyeX, float eyeY, float eyeZ,
                 float centerX, float centerY, float centerZ,
                 float upX, float upY, float upZ)
{
    glm::mat4 result = glm::lookAt(
        glm::vec3(eyeX, eyeY, eyeZ),
        glm::vec3(centerX, centerY, centerZ),
        glm::vec3(upX, upY, upZ));
    std::memcpy(m, glm::value_ptr(result), 16 * sizeof(float));
}

void math_ortho(float *m, float left, float right, float bottom, float top, float zNear, float zFar)
{
    glm::mat4 result = glm::ortho(left, right, bottom, top, zNear, zFar);
    std::memcpy(m, glm::value_ptr(result), 16 * sizeof(float));
}