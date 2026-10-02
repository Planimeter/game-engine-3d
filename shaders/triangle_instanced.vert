#version 450
/* Copyright Planimeter. All Rights Reserved. */

/* Instanced default 3D vertex shader (Vulkan).
 *
 * The model matrix arrives per instance as vertex attributes 5-8 (a mat4's
 * four columns) from vertex buffer binding 1, stepped with
 * VK_VERTEX_INPUT_RATE_INSTANCE, so one draw covers `count` placements. The
 * view-projection still rides in push constants and is shared by every
 * instance. Material layout must match vulkan_material_pack(). */

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_tangent;
layout(location = 3) in vec3 in_bitangent;
layout(location = 4) in vec2 in_texcoord;

layout(location = 5) in vec4 in_m0;
layout(location = 6) in vec4 in_m1;
layout(location = 7) in vec4 in_m2;
layout(location = 8) in vec4 in_m3;

layout(push_constant) uniform Transform {
    mat4 mvp;
    mat4 model;
} uTransform;

layout(set = 1, binding = 1) uniform Material {
    vec4 floats[8];
    vec4 vec3s[16];
    mat4 model;
} uMaterial;

layout(location = 0) out vec2 v_texcoord;
layout(location = 1) out vec3 v_worldNormal;
layout(location = 2) out vec3 v_worldPos;

void main()
{
    mat4 instanceModel = mat4(in_m0, in_m1, in_m2, in_m3);

    vec4 worldPos = instanceModel * vec4(in_position, 1.0);
    v_worldPos = worldPos.xyz;
    /* Instance matrices are rotation+translation, so the upper 3x3 works as
     * a normal matrix */
    v_worldNormal = normalize(mat3(instanceModel) * in_normal);
    v_texcoord = in_texcoord;

    gl_Position = uTransform.mvp * worldPos;
    /* Engine matrices are GL-style (y up); Vulkan's clip space is y down */
    gl_Position.y = -gl_Position.y;
}
