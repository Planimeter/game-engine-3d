/* Copyright Planimeter. All Rights Reserved. */

#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

/* Forward declarations */
static Mesh *model_processmesh(const struct aiMesh *aiMesh, const struct aiScene *scene);
static Material model_processmaterial(const struct aiMaterial *aiMat);

static Mesh *model_processmesh(const struct aiMesh *aiMesh, const struct aiScene *scene)
{
    Mesh *mesh = (Mesh *)malloc(sizeof(Mesh));
    if (!mesh) {
        fprintf(stderr, "Failed to allocate memory for mesh\n");
        return NULL;
    }

    /* Copy vertices */
    mesh->vertexCount = aiMesh->mNumVertices;
    mesh->vertices = (Vertex *)malloc(mesh->vertexCount * sizeof(Vertex));
    if (!mesh->vertices) {
        fprintf(stderr, "Failed to allocate memory for vertices\n");
        free(mesh);
        return NULL;
    }

    for (uint32_t i = 0; i < mesh->vertexCount; i++) {
        mesh->vertices[i].position = glm::vec3(
            aiMesh->mVertices[i].x,
            aiMesh->mVertices[i].y,
            aiMesh->mVertices[i].z
        );

        if (aiMesh->HasNormals()) {
            mesh->vertices[i].normal = glm::vec3(
                aiMesh->mNormals[i].x,
                aiMesh->mNormals[i].y,
                aiMesh->mNormals[i].z
            );
        } else {
            mesh->vertices[i].normal = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        if (aiMesh->mTextureCoords[0]) {
            mesh->vertices[i].texCoords = glm::vec2(
                aiMesh->mTextureCoords[0][i].x,
                aiMesh->mTextureCoords[0][i].y
            );
        } else {
            mesh->vertices[i].texCoords = glm::vec2(0.0f, 0.0f);
        }

        if (aiMesh->HasTangentsAndBitangents()) {
            mesh->vertices[i].tangent = glm::vec3(
                aiMesh->mTangents[i].x,
                aiMesh->mTangents[i].y,
                aiMesh->mTangents[i].z
            );
            mesh->vertices[i].bitangent = glm::vec3(
                aiMesh->mBitangents[i].x,
                aiMesh->mBitangents[i].y,
                aiMesh->mBitangents[i].z
            );
        } else {
            mesh->vertices[i].tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            mesh->vertices[i].bitangent = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    /* Copy indices */
    uint32_t indexCount = 0;
    for (uint32_t i = 0; i < aiMesh->mNumFaces; i++) {
        indexCount += aiMesh->mFaces[i].mNumIndices;
    }
    mesh->indexCount = indexCount;

    mesh->indices = (uint32_t *)malloc(mesh->indexCount * sizeof(uint32_t));
    if (!mesh->indices) {
        fprintf(stderr, "Failed to allocate memory for indices\n");
        free(mesh->vertices);
        free(mesh);
        return NULL;
    }

    uint32_t indexOffset = 0;
    for (uint32_t i = 0; i < aiMesh->mNumFaces; i++) {
        const struct aiFace *face = &aiMesh->mFaces[i];
        for (uint32_t j = 0; j < face->mNumIndices; j++) {
            mesh->indices[indexOffset++] = face->mIndices[j];
        }
    }

    /* Process material */
    if (aiMesh->mMaterialIndex >= 0 && scene->mMaterials) {
        mesh->material = model_processmaterial(scene->mMaterials[aiMesh->mMaterialIndex]);
    } else {
        /* Default material */
        mesh->material.ambient = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
        mesh->material.diffuse = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
        mesh->material.specular = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        mesh->material.emission = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        mesh->material.shininess = 32.0f;
        mesh->material.ambientTexture = NULL;
        mesh->material.diffuseTexture = NULL;
        mesh->material.specularTexture = NULL;
        mesh->material.normalTexture = NULL;
    }

    /* Initialize buffer pointers to NULL (to be allocated later by graphics system) */
    mesh->vertexBuffer = NULL;
    mesh->indexBuffer = NULL;
    mesh->vertexBufferMemory = NULL;
    mesh->indexBufferMemory = NULL;

    return mesh;
}

static Material model_processmaterial(const struct aiMaterial *aiMat)
{
    Material material;

    aiColor4D ambient = {0.2f, 0.2f, 0.2f, 1.0f};
    aiColor4D diffuse = {0.8f, 0.8f, 0.8f, 1.0f};
    aiColor4D specular = {1.0f, 1.0f, 1.0f, 1.0f};
    aiColor4D emission = {0.0f, 0.0f, 0.0f, 1.0f};

    aiGetMaterialColor(aiMat, AI_MATKEY_COLOR_AMBIENT, &ambient);
    aiGetMaterialColor(aiMat, AI_MATKEY_COLOR_DIFFUSE, &diffuse);
    aiGetMaterialColor(aiMat, AI_MATKEY_COLOR_SPECULAR, &specular);
    aiGetMaterialColor(aiMat, AI_MATKEY_COLOR_EMISSIVE, &emission);

    material.ambient = glm::vec4(ambient.r, ambient.g, ambient.b, ambient.a);
    material.diffuse = glm::vec4(diffuse.r, diffuse.g, diffuse.b, diffuse.a);
    material.specular = glm::vec4(specular.r, specular.g, specular.b, specular.a);
    material.emission = glm::vec4(emission.r, emission.g, emission.b, emission.a);

    aiGetMaterialFloat(aiMat, AI_MATKEY_SHININESS, &material.shininess);

    /* Texture names (simplified - just extract first if available) */
    struct aiString path;

    if (aiGetMaterialString(aiMat, AI_MATKEY_TEXTURE_DIFFUSE(0), &path) == AI_SUCCESS) {
        material.diffuseTexture = (char *)malloc(strlen(path.data) + 1);
        if (material.diffuseTexture) {
            strcpy(material.diffuseTexture, path.data);
        }
    } else {
        material.diffuseTexture = NULL;
    }

    if (aiGetMaterialString(aiMat, AI_MATKEY_TEXTURE_NORMALS(0), &path) == AI_SUCCESS) {
        material.normalTexture = (char *)malloc(strlen(path.data) + 1);
        if (material.normalTexture) {
            strcpy(material.normalTexture, path.data);
        }
    } else {
        material.normalTexture = NULL;
    }

    material.ambientTexture = NULL;
    material.specularTexture = NULL;

    return material;
}

Model *model_load(const char *filepath)
{
    if (!filepath) {
        fprintf(stderr, "Invalid filepath\n");
        return NULL;
    }

    const struct aiScene *scene = aiImportFile(
        filepath,
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace |
        aiProcess_GenNormals |
        aiProcess_OptimizeMeshes |
        aiProcess_RemoveRedundantMaterials
    );

    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        fprintf(stderr, "Assimp Error: %s\n", aiGetErrorString());
        if (scene) {
            aiReleaseImport(scene);
        }
        return NULL;
    }

    Model *model = (Model *)malloc(sizeof(Model));
    if (!model) {
        fprintf(stderr, "Failed to allocate memory for model\n");
        aiReleaseImport(scene);
        return NULL;
    }

    /* Set model name */
    model->name = (char *)malloc(strlen(filepath) + 1);
    if (model->name) {
        strcpy(model->name, filepath);
    }

    /* Process all meshes */
    model->meshCount = scene->mNumMeshes;
    model->meshes = (Mesh *)malloc(model->meshCount * sizeof(Mesh));
    if (!model->meshes) {
        fprintf(stderr, "Failed to allocate memory for meshes\n");
        free(model->name);
        free(model);
        aiReleaseImport(scene);
        return NULL;
    }

    for (uint32_t i = 0; i < model->meshCount; i++) {
        Mesh *mesh = model_processmesh(scene->mMeshes[i], scene);
        if (mesh) {
            model->meshes[i] = *mesh;
            free(mesh);
        } else {
            fprintf(stderr, "Failed to process mesh %d\n", i);
        }
    }

    aiReleaseImport(scene);

    printf("Model loaded: %s with %d meshes\n", filepath, model->meshCount);

    return model;
}

void model_destroy(Model *model)
{
    if (!model) {
        return;
    }

    if (model->meshes) {
        for (uint32_t i = 0; i < model->meshCount; i++) {
            Mesh *mesh = &model->meshes[i];
            
            if (mesh->vertices) {
                free(mesh->vertices);
            }
            if (mesh->indices) {
                free(mesh->indices);
            }
            if (mesh->material.ambientTexture) {
                free(mesh->material.ambientTexture);
            }
            if (mesh->material.diffuseTexture) {
                free(mesh->material.diffuseTexture);
            }
            if (mesh->material.specularTexture) {
                free(mesh->material.specularTexture);
            }
            if (mesh->material.normalTexture) {
                free(mesh->material.normalTexture);
            }
        }
        free(model->meshes);
    }

    if (model->name) {
        free(model->name);
    }

    free(model);
}
