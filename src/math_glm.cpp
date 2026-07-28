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