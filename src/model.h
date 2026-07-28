/* Copyright Planimeter. All Rights Reserved. */

#ifndef MODEL_H
#define MODEL_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

typedef struct {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
    glm::vec3 tangent;
    glm::vec3 bitangent;
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
} Mesh;

typedef struct {
    Mesh *meshes;
    uint32_t meshCount;
    char *name;
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

#ifdef __cplusplus
}
#endif

#endif /* MODEL_H */
