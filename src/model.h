/* Copyright Planimeter. All Rights Reserved. */

#ifndef MODEL_H
#define MODEL_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>

/* Skinning: up to 4 bone influences per vertex (the common convention, and
 * what most exporters emit). boneIndices are indices into Model::bones. */
#define MAX_BONE_INFLUENCES 4

typedef struct {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    /* Skin weights. Unused for static models: boneIndices all zero and
     * boneWeights all 1 on bone 0 gives the identity transform. */
    glm::vec4 boneWeights;
    glm::ivec4 boneIndices;
} Vertex;

typedef struct {
    glm::vec4 ambient;
    glm::vec4 diffuse;
    glm::vec4 specular;
    glm::vec4 emission;
    float shininess;
    char *ambientTexture;
    char *diffuseTexture;
    char *specularTexture;
    char *normalTexture;
} MaterialData;

typedef struct {
    Vertex *vertices;
    uint32_t *indices;
    uint32_t vertexCount;
    uint32_t indexCount;
    MaterialData material;
    void *vertexBuffer;
    void *indexBuffer;
    void *vertexBufferMemory;
    void *indexBufferMemory;
    /* Loaded GPU textures, or NULL when the material had no such map.
     * Populated by model_loadtextures(). Slot assignment lives in the
     * backend: Metal reuses texture slot 0 for models and the text renderer
     * rebinds it afterwards, while Vulkan uses the bindless array. */
    void *diffuseTexture;
    void *normalTexture;
} Mesh;

typedef struct ModelAnimation ModelAnimation;

typedef struct {
    Mesh *meshes;
    uint32_t meshCount;
    char *name;

    /* Skeleton. boneOffset is this bone's transform relative to its parent,
     * so flattening the chain by multiplying parent*offset gives the final
     * skinning matrix directly. */
    glm::mat4 *boneOffset;
    glm::mat4 *boneInverseBind;  /* inverse bind pose, per bone */
    int *boneParent;             /* -1 for roots */
    uint32_t boneCount;

    /* Animation clips. Frames are already in bone-local space; pose by
     * multiplying offset * animatedLocal per bone. */
    ModelAnimation *animations;
    uint32_t animationCount;
} Model;
#else
typedef struct Vertex Vertex;
typedef struct MaterialData MaterialData;
typedef struct Mesh Mesh;
typedef struct Model Model;
typedef struct ModelAnimation ModelAnimation;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Model loading and management */
Model *model_load(const char *filepath);
void   model_destroy(Model *model);

#ifdef __cplusplus
/* One animation clip: a keyframe track per bone, sampled uniformly in time. */
struct ModelAnimation {
    char *name;
    float duration;            /* seconds */
    float ticksPerSecond;
    uint32_t keyCount;         /* frames per bone track */
    glm::mat4 *frames;         /* boneCount * keyCount, bone-major */
};

/* Flatten the bone hierarchy at `time` into skinning matrices:
 * out[i] = accumulatedWorld(bone i) * inverseBind(i). Bone-local animated
 * transforms are sampled from the clip (or the rest pose when clip is NULL).
 * Returns 0 if the model has no skeleton. */
int  model_pose(Model *model, const ModelAnimation *clip, float time,
                glm::mat4 *out);

const ModelAnimation *model_get_animation(const Model *model, uint32_t index);
#endif

/* Scale and centre the mesh data so the model fits a box of about
 * targetHeight units tall, centred on the origin. Applied to vertex
 * positions and normals (and bone offsets, so the skeleton follows) so an
 * imported asset lands somewhere the default camera can see it.
 * Does nothing for a model with no meshes. */
void model_normalize(Model *model, float targetHeight);

/* Load each mesh's diffuse texture named in its material, relative to
 * `modelDir` (usually the model file's directory), and assign bindless
 * slots. Slot 0 is reserved for the font atlas, so model textures start
 * at 1. Returns the number of textures loaded. */
int model_loadtextures(Model *model, const char *modelDir);

/* Same pose query addressed by clip index, so C callers do not need the
 * C++-only ModelAnimation type. Pass clipIndex < 0 for the rest pose.
 * out16 receives boneCount * 16 floats, column-major. */
int model_pose_by_index(Model *model, int clipIndex, float time, float *out16);

/* C-compatible accessors for opaque Model struct */
uint32_t model_get_mesh_count(const Model *model);
uint32_t model_get_bone_count(const Model *model);
uint32_t model_get_animation_count(const Model *model);
int   model_is_skinned(const Model *model);

/* Opaque clip info, so C callers can report on animations without needing
 * the C++-only ModelAnimation definition. */
typedef struct {
    const char *name;
    float duration;
    uint32_t keyCount;
} ModelAnimationInfo;

int model_get_animation_info(const Model *model, uint32_t index, ModelAnimationInfo *out);

#ifdef __cplusplus
}
#endif

#endif /* MODEL_H */
