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

typedef struct {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    glm::uvec4 boneIDs;
    glm::vec4 boneWeights;
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
    uint32_t *boneIndices;   /* per-vertex: which bones affect this vertex */
    float *boneWeights;      /* per-vertex: weight for each bone */
    uint32_t *boneCounts;    /* per-vertex: how many bones affect this vertex */
    uint32_t boneCount;      /* number of bones referenced by this mesh */
} Mesh;

typedef struct {
    Mesh *meshes;
    uint32_t meshCount;
    char *name;

    /* Skeletal animation data */
    glm::mat4 *boneOffsets;     /* inverse bind-pose matrix per bone */
    glm::mat4 *boneTransforms;  /* current world-space bone transforms */
    uint32_t boneCount;
    char **boneNames;           /* bone name per bone */
    float animationTime;        /* current playback time in seconds */
    float animationDuration;    /* total duration of current animation */
    uint32_t animationIndex;    /* which animation clip is active */
    uint32_t animationCount;    /* number of available animation clips */
    char **animationNames;      /* name per animation clip */
    void *animationData;        /* opaque: AnimationClip[] */
    void *sceneNodes;           /* opaque: SceneNode[] */
    uint32_t sceneNodeCount;
} Model;
#else
typedef struct Vertex Vertex;
typedef struct MaterialData MaterialData;
typedef struct Mesh Mesh;
typedef struct Model Model;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Model loading and management */
Model *model_load(const char *filepath);
void   model_destroy(Model *model);

/* C-compatible accessors for opaque Model struct */
uint32_t model_get_mesh_count(const Model *model);

/* Skeletal animation */
void     model_update(Model *model, float deltaTime);
uint32_t model_get_bone_count(const Model *model);
uint32_t model_get_animation_count(const Model *model);
void     model_set_animation(Model *model, uint32_t index);
const char *model_get_animation_name(const Model *model, uint32_t index);
const float *model_get_bone_matrices(const Model *model);

#ifdef __cplusplus
}
#endif

#endif /* MODEL_H */
