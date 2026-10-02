#version 450
/* Copyright Planimeter. All Rights Reserved. */

/* Default 3D vertex shader (Vulkan).
 * Per-draw transforms (mvp + world) ride in push constants so several draws
 * per frame can't clobber shared uniform memory; material at set 1 binding 1.
 * Material layout must match vulkan_material_pack():
 *   floats[8]  (vec4)  — [0].x/y/z = ambient/specular/shininess
 *   vec3s[16]  (vec4)  — [0] light dir, [1] camera pos, [2] albedo
 *   model      (mat4)  — kept for layout parity; world matrix comes from
 *                        the push constant block above */

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_tangent;
layout(location = 3) in vec3 in_bitangent;
layout(location = 4) in vec2 in_texcoord;

/* Skinning. Static models carry bone 0 at weight 1, so this collapses to
 * the identity and one pipeline serves both cases. */
layout(location = 5) in vec4 in_boneWeights;
layout(location = 6) in ivec4 in_boneIndices;

/* Skinning matrices at set 1, binding 2. MAX_SKIN_BONES must match
 * MAX_SKIN_BONES in graphics_vulkan.cpp. */
#define MAX_SKIN_BONES 64
layout(set = 1, binding = 2) uniform SkinData {
    mat4 bones[MAX_SKIN_BONES];
} uSkin;

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
    /* Blend the four bone matrices by weight. With the static default
     * (bone 0, weight 1) this is exactly uSkin.bones[0]. */
    mat4 skin = uSkin.bones[in_boneIndices.x] * in_boneWeights.x
              + uSkin.bones[in_boneIndices.y] * in_boneWeights.y
              + uSkin.bones[in_boneIndices.z] * in_boneWeights.z
              + uSkin.bones[in_boneIndices.w] * in_boneWeights.w;

    /* Normals need the rotation part only; skipping translation here keeps
     * lighting correct regardless of bone offsets. */
    mat3 skinNormal = mat3(skin);

    vec4 localPos = skin * vec4(in_position, 1.0);
    vec4 worldPos = uTransform.model * localPos;
    v_worldPos = worldPos.xyz;
    /* Model matrices are rotation+translation, so the upper 3x3 works as
     * a normal matrix */
    v_worldNormal = normalize(mat3(uTransform.model) * skinNormal * in_normal);
    v_texcoord = in_texcoord;

    gl_Position = uTransform.mvp * worldPos;
    /* Engine matrices are GL-style (y up); Vulkan's clip space is y down */
    gl_Position.y = -gl_Position.y;
}
