#version 450
/* Copyright Planimeter. All Rights Reserved. */

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_tangent;
layout(location = 3) in vec3 in_bitangent;
layout(location = 4) in vec2 in_texcoord;
layout(location = 5) in uvec4 in_boneIDs;
layout(location = 6) in vec4 in_boneWeights;

layout(set = 1, binding = 0) uniform UBO {
    mat4 mvp;
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambientColor;
} ubo;

#define MAX_BONES 256
layout(set = 1, binding = 1) uniform Skinning {
    mat4 boneMatrices[MAX_BONES];
} skinning;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 TexCoord;

void main()
{
    vec4 skinnedPos = vec4(in_position, 1.0);
    vec3 skinnedNormal = in_normal;

    float totalWeight = in_boneWeights.x + in_boneWeights.y + in_boneWeights.z + in_boneWeights.w;
    if (totalWeight > 0.0) {
        mat4 skinMatrix =
            in_boneWeights.x * skinning.boneMatrices[in_boneIDs.x] +
            in_boneWeights.y * skinning.boneMatrices[in_boneIDs.y] +
            in_boneWeights.z * skinning.boneMatrices[in_boneIDs.z] +
            in_boneWeights.w * skinning.boneMatrices[in_boneIDs.w];
        skinnedPos = skinMatrix * vec4(in_position, 1.0);
        skinnedNormal = mat3(skinMatrix) * in_normal;
    }

    vec4 worldPos = ubo.model * skinnedPos;
    gl_Position = ubo.projection * ubo.view * worldPos;

    FragPos = worldPos.xyz;
    Normal = mat3(transpose(inverse(ubo.model))) * skinnedNormal;
    TexCoord = in_texcoord;
}
