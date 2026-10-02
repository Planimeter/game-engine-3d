#version 450
#extension GL_EXT_nonuniform_qualifier : require
/* Copyright Planimeter. All Rights Reserved. */

/* Default 3D fragment shader (Vulkan): Blinn-Phong.
 * Material layout must match vulkan_material_pack() — see triangle.vert. */

layout(location = 0) in vec2 v_texcoord;
layout(location = 1) in vec3 v_worldNormal;
layout(location = 2) in vec3 v_worldPos;

layout(location = 0) out vec4 out_color;

/* Bindless texture array at set 0. diffuseSlot is -1 when the material has
 * no diffuse map, in which case the material albedo is used unchanged. */
layout(set = 0, binding = 0) uniform sampler2D tex[];

layout(set = 1, binding = 1) uniform Material {
    vec4 floats[8];
    vec4 vec3s[16];
    mat4 model;
} uMaterial;

void main()
{
    /* floats[1].x = diffuse texture slot (-1 = none), .y = has-normal-map */
    float diffuseSlot = uMaterial.floats[1].x;

    float ambient = uMaterial.floats[0].x;
    float specular = uMaterial.floats[0].y;
    float shininess = uMaterial.floats[0].z;
    vec3 lightDir = uMaterial.vec3s[0].xyz;
    vec3 cameraPos = uMaterial.vec3s[1].xyz;
    vec3 albedo = uMaterial.vec3s[2].xyz;

    /* Unbound entries in the partially-bound array read as zero, which would
     * black out the model; fall back to the material colour. */
    vec4 sampled = (diffuseSlot >= 0.0) ? texture(tex[int(diffuseSlot)], v_texcoord)
                                         : vec4(1.0);
    if (any(greaterThan(sampled.rgb, vec3(0.0)))) {
        albedo *= sampled.rgb;
    }

    vec3 N = normalize(v_worldNormal);
    vec3 L = normalize(-lightDir); /* direction toward the light */
    vec3 V = normalize(cameraPos - v_worldPos);
    vec3 H = normalize(L + V);

    float ndl = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), max(shininess, 1.0)) * specular;
    vec3 color = albedo * (ambient + (1.0 - ambient) * ndl) + spec;

    out_color = vec4(color, 1.0);
}
