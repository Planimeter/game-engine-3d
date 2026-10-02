#version 450
/* Copyright Planimeter. All Rights Reserved. */

/* Instanced default 3D vertex shader (Vulkan).
 *
 * Per-instance transforms come from a uniform buffer at set 1, binding 2,
 * indexed by gl_InstanceIndex. The conventional alternative — a second
 * vertex buffer with VK_VERTEX_INPUT_RATE_INSTANCE feeding locations 5-8 —
 * crashes MoltenVK's SPIR-V-to-MSL translator inside
 * MVKShaderModule::getMTLFunction, so instance data is read as a uniform
 * array instead. One draw call still covers `count` placements.
 *
 * The view-projection still rides in push constants and is shared by every
 * instance. Material layout must match vulkan_material_pack(). */

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_tangent;
layout(location = 3) in vec3 in_bitangent;
layout(location = 4) in vec2 in_texcoord;

layout(push_constant) uniform Transform {
    mat4 mvp;
    mat4 model;
} uTransform;

/* Must match MAX_INSTANCES in graphics_vulkan.cpp */
#define MAX_INSTANCES 256

layout(set = 1, binding = 2) uniform InstanceData {
    mat4 transforms[MAX_INSTANCES];
} uInstances;

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
    mat4 instanceModel = uInstances.transforms[gl_InstanceIndex];

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
