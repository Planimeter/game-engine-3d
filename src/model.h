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

/* Both renderer paths support four bone influences per vertex. */
#define MAX_BONE_INFLUENCES 4

typedef struct {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    glm::vec4 boneWeights;
    /* The legacy Metal/local Vulkan path interprets indices as signed;
     * the incoming Vulkan skinning path uses unsigned bone IDs. The storage
     * and size are identical, while each backend selects its matching view. */
    union {
        glm::ivec4 boneIndices;
        glm::uvec4 boneIDs;
    };
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

#define MAX_BONES_PER_VERTEX 4

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
    /* Loaded GPU textures, or NULL when the material has no such map. */
    void *diffuseTexture;
    void *normalTexture;
    /* Optional per-vertex influence arrays used by import paths that retain
     * variable influence counts rather than packing directly into Vertex. */
    uint32_t *boneIndices;
    float *boneWeights;
    uint32_t *boneCounts;
    uint32_t boneCount;
} Mesh;

typedef struct ModelAnimation ModelAnimation;

typedef struct {
    Mesh *meshes;
    uint32_t meshCount;
    char *name;

    /* Hierarchy-based pose representation used by model_pose(). */
    glm::mat4 *boneOffset;
    glm::mat4 *boneInverseBind;
    int *boneParent;
    uint32_t boneCount;
    ModelAnimation *animations;

    /* Scene-node animation representation used by model_update(). */
    glm::mat4 *boneOffsets;
    glm::mat4 *boneTransforms;
    char **boneNames;
    float animationTime;
    float animationDuration;
    uint32_t animationIndex;
    char **animationNames;
    void *animationData;
    void *sceneNodes;
    uint32_t sceneNodeCount;
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
    float duration;
    float ticksPerSecond;
    uint32_t keyCount;
    glm::mat4 *frames;
};

int model_pose(Model *model, const ModelAnimation *clip, float time,
               glm::mat4 *out);
const ModelAnimation *model_get_animation(const Model *model, uint32_t index);
#endif

void model_normalize(Model *model, float targetHeight);
int model_loadtextures(Model *model, const char *modelDir);
int model_pose_by_index(Model *model, int clipIndex, float time, float *out16);

/* C-compatible accessors for opaque Model struct. */
uint32_t model_get_mesh_count(const Model *model);
uint32_t model_get_bone_count(const Model *model);
uint32_t model_get_animation_count(const Model *model);
int model_is_skinned(const Model *model);

typedef struct {
    const char *name;
    float duration;
    uint32_t keyCount;
} ModelAnimationInfo;

int model_get_animation_info(const Model *model, uint32_t index, ModelAnimationInfo *out);

/* Scene-node skeletal animation API. */
void model_update(Model *model, float deltaTime);
void model_set_animation(Model *model, uint32_t index);
const char *model_get_animation_name(const Model *model, uint32_t index);
const float *model_get_bone_matrices(const Model *model);

#ifdef __cplusplus
}
#endif

#endif /* MODEL_H */
