/* Copyright Planimeter. All Rights Reserved. */

#include "model.h"
#include "image.h"
#include "graphics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <assimp/anim.h>
#include <assimp/quaternion.h>

#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>

/* Forward declarations */
/* Assimp stores bones per-mesh (aiMesh::mBones), not on the scene, and the
 * same skeleton is repeated across every mesh. Bones are therefore collected
 * into a unique-name table first; aiBone::mOffsetMatrix is the inverse bind
 * matrix, while the bone's local transform comes from the node hierarchy. */
typedef struct {
    char **names;
    glm::mat4 *inverseBind;
    uint32_t count;
    uint32_t capacity;
} BoneTable;

static void model_processanimations(const struct aiScene *scene, Model *model,
                                    const BoneTable *table);

static Mesh *model_processmesh(const struct aiMesh *aiMesh, const struct aiScene *scene);
static MaterialData model_processmaterial(const struct aiMaterial *aiMat);

static int bonetable_find(const BoneTable *table, const char *name)
{
    for (uint32_t i = 0; i < table->count; i++) {
        if (strcmp(table->names[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int bonetable_add(BoneTable *table, const char *name,
                         const glm::mat4 &inverseBind)
{
    if (table->count == table->capacity) {
        uint32_t cap = table->capacity ? table->capacity * 2 : 32;
        char **names = (char **)realloc(table->names, cap * sizeof(char *));
        glm::mat4 *binds = (glm::mat4 *)realloc(table->inverseBind, cap * sizeof(glm::mat4));
        if (!names || !binds) {
            free(names);
            free(binds);
            return -1;
        }
        table->names = names;
        table->inverseBind = binds;
        table->capacity = cap;
    }

    table->names[table->count] = (char *)malloc(strlen(name) + 1);
    if (!table->names[table->count]) {
        return -1;
    }
    strcpy(table->names[table->count], name);
    table->inverseBind[table->count] = inverseBind;
    return (int)table->count++;
}

static void bonetable_free(BoneTable *table)
{
    for (uint32_t i = 0; i < table->count; i++) {
        free(table->names[i]);
    }
    free(table->names);
    free(table->inverseBind);
    memset(table, 0, sizeof(*table));
}

static glm::mat4 model_assimp_matrix(const struct aiMatrix4x4t<float> *m)
{
    /* Assimp's aiMatrix4x4t stores columns in a/b/c/d, which is already the
     * layout GLM wants. Transposing here flips the translation into the
     * wrong axis and mirrors the model. */
    return glm::mat4(
        m->a1, m->a2, m->a3, m->a4,
        m->b1, m->b2, m->b3, m->b4,
        m->c1, m->c2, m->c3, m->c4,
        m->d1, m->d2, m->d3, m->d4);
}

static void model_walknodes(const BoneTable *table, const struct aiNode *node,
                            int parentBone, Model *model)
{
    int self = bonetable_find(table, node->mName.C_Str());

    if (self >= 0) {
        model->boneParent[self] = parentBone;
        model->boneOffset[self] = model_assimp_matrix(&node->mTransformation);
    }

    for (unsigned c = 0; c < node->mNumChildren; c++) {
        model_walknodes(table, node->mChildren[c], self, model);
    }
}

static void model_processskeleton(const struct aiScene *scene, Model *model)
{
    BoneTable table = {0};

    model->boneOffset = NULL;
    model->boneInverseBind = NULL;
    model->boneParent = NULL;
    model->boneCount = 0;

    for (uint32_t m = 0; m < scene->mNumMeshes; m++) {
        const struct aiMesh *mesh = scene->mMeshes[m];
        for (unsigned b = 0; b < mesh->mNumBones; b++) {
            const struct aiBone *bone = mesh->mBones[b];
            if (bonetable_find(&table, bone->mName.C_Str()) >= 0) {
                continue;
            }
            glm::mat4 invBind = model_assimp_matrix(&bone->mOffsetMatrix);
            if (bonetable_add(&table, bone->mName.C_Str(), invBind) < 0) {
                fprintf(stderr, "Failed to build bone table\n");
                bonetable_free(&table);
                return;
            }
        }
    }

    if (table.count > 0) {
        model->boneCount = table.count;
        model->boneOffset = (glm::mat4 *)malloc(table.count * sizeof(glm::mat4));
        model->boneInverseBind = (glm::mat4 *)malloc(table.count * sizeof(glm::mat4));
        model->boneParent = (int *)malloc(table.count * sizeof(int));
        if (model->boneOffset && model->boneInverseBind && model->boneParent) {
            for (uint32_t i = 0; i < table.count; i++) {
                model->boneInverseBind[i] = table.inverseBind[i];
                model->boneOffset[i] = glm::mat4(1.0f);
                model->boneParent[i] = -1;
            }
            model_walknodes(&table, scene->mRootNode, -1, model);
        } else {
            fprintf(stderr, "Failed to allocate bone arrays\n");
            free(model->boneOffset);
            free(model->boneInverseBind);
            free(model->boneParent);
            model->boneOffset = NULL;
            model->boneInverseBind = NULL;
            model->boneParent = NULL;
            model->boneCount = 0;
        }
    }

    /* Animation channels reference bones by name, so they need the table
     * before it goes away. */
    model_processanimations(scene, model, &table);
    bonetable_free(&table);
}

static void model_processanimations(const struct aiScene *scene, Model *model,
                                    const BoneTable *table)
{
    model->animations = NULL;
    model->animationCount = 0;

    if (scene->mNumAnimations == 0) {
        return;
    }

    model->animations = (ModelAnimation *)calloc(scene->mNumAnimations, sizeof(ModelAnimation));
    if (!model->animations) {
        return;
    }

    /* Uniform sampling: find the highest key count across all channels so
     * every bone shares one time grid. */
    for (uint32_t a = 0; a < scene->mNumAnimations; a++) {
        const struct aiAnimation *anim = scene->mAnimations[a];
        ModelAnimation *out = &model->animations[a];
        uint32_t maxKeys = 0;

        for (unsigned c = 0; c < anim->mNumChannels; c++) {
            const struct aiNodeAnim *ch = anim->mChannels[c];
            unsigned n = ch->mNumPositionKeys;
            if (ch->mNumRotationKeys   > n) n = ch->mNumRotationKeys;
            if (ch->mNumScalingKeys    > n) n = ch->mNumScalingKeys;
            if (n > maxKeys) {
                maxKeys = n;
            }
        }
        if (maxKeys == 0 || model->boneCount == 0) {
            continue;
        }

        out->name = (char *)malloc(strlen(anim->mName.C_Str()) + 1);
        if (out->name) {
            strcpy(out->name, anim->mName.C_Str());
        }
        out->ticksPerSecond = anim->mTicksPerSecond > 0.0f ? anim->mTicksPerSecond : 25.0f;
        out->duration = (float)anim->mDuration / out->ticksPerSecond;
        out->keyCount = maxKeys;
        out->frames = (glm::mat4 *)calloc((size_t)model->boneCount * maxKeys, sizeof(glm::mat4));

        if (!out->frames) {
            free(out->name);
            out->name = NULL;
            continue;
        }

        /* Bones with no channel stay at identity; model_pose applies the
         * rest offset separately, so identity is the right default here. */
        for (size_t i = 0; i < (size_t)model->boneCount * maxKeys; i++) {
            out->frames[i] = glm::mat4(1.0f);
        }

        for (unsigned c = 0; c < anim->mNumChannels; c++) {
            const struct aiNodeAnim *ch = anim->mChannels[c];
            int boneIndex = bonetable_find(table, ch->mNodeName.C_Str());
            if (boneIndex < 0) {
                continue;
            }

            /* Each key type has its own array and count. Where a channel is
             * missing a track, identity is correct: translation 0, rotation
             * identity, scale 1. */
            for (uint32_t k = 0; k < maxKeys; k++) {
                glm::vec3 t(0.0f);
                glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
                glm::vec3 s(1.0f);

                if (k < ch->mNumPositionKeys) {
                    t = glm::vec3(ch->mPositionKeys[k].mValue.x,
                                  ch->mPositionKeys[k].mValue.y,
                                  ch->mPositionKeys[k].mValue.z);
                }
                if (k < ch->mNumRotationKeys) {
                    const struct aiQuatKey &qk = ch->mRotationKeys[k];
                    r = glm::quat(qk.mValue.w, qk.mValue.x, qk.mValue.y, qk.mValue.z);
                }
                if (k < ch->mNumScalingKeys) {
                    s = glm::vec3(ch->mScalingKeys[k].mValue.x,
                                  ch->mScalingKeys[k].mValue.y,
                                  ch->mScalingKeys[k].mValue.z);
                }

                out->frames[(size_t)boneIndex * maxKeys + k] =
                    glm::translate(glm::mat4(1.0f), t) *
                    glm::mat4_cast(r) *
                    glm::scale(glm::mat4(1.0f), s);
            }
        }

        model->animationCount++;
    }
}

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

        /* Skin weights. Default is bone 0 at full weight, which is the
         * identity when a model has no skeleton. */
        mesh->vertices[i].boneIndices = glm::ivec4(0);
        mesh->vertices[i].boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);

        if (aiMesh->HasBones() && aiMesh->mNumBones > 0 && aiMesh->mBones[0]->mNumWeights > 0) {
            const struct aiBone *bone = aiMesh->mBones[0];
            for (unsigned w = 0; w < bone->mNumWeights && w < MAX_BONE_INFLUENCES; w++) {
                mesh->vertices[i].boneIndices[w] = (int)bone->mWeights[w].mVertexId;
                mesh->vertices[i].boneWeights[w] = (float)bone->mWeights[w].mWeight;
            }
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

static MaterialData model_processmaterial(const struct aiMaterial *aiMat)
{
    MaterialData material;

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
        aiProcess_RemoveRedundantMaterials |
        /* Caps skinning to MAX_BONE_INFLUENCES; a no-op on static models
         * like cube.obj. Assimp populates aiBone/aiAnimation without any
         * extra flag. */
        aiProcess_LimitBoneWeights
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

    uint32_t processedCount = 0;
    for (uint32_t i = 0; i < model->meshCount; i++) {
        Mesh *mesh = model_processmesh(scene->mMeshes[i], scene);
        if (mesh) {
            model->meshes[i] = *mesh;
            free(mesh);
            processedCount++;
        } else {
            fprintf(stderr, "Failed to process mesh %d\n", i);
        }
    }

    if (processedCount != model->meshCount) {
        fprintf(stderr, "Failed to load model '%s': only %u/%u meshes processed successfully\n",
                filepath, processedCount, model->meshCount);
        model_destroy(model);
        aiReleaseImport(scene);
        return NULL;
    }

    /* Skeleton and animation clips. Both optional: a static model has neither.
     * Done before aiReleaseImport because they read from the scene. */
    model_processskeleton(scene, model);

    aiReleaseImport(scene);

    printf("Model loaded: %s with %d meshes", filepath, model->meshCount);
    if (model->boneCount > 0) {
        printf(", %u bones", model->boneCount);
    }
    if (model->animationCount > 0) {
        printf(", %u animations", model->animationCount);
    }
    printf("\n");

    return model;
}

void model_normalize(Model *model, float targetHeight)
{
    if (!model || !model->meshes || model->meshCount == 0 || targetHeight <= 0.0f) {
        return;
    }

    float minY = 0.0f, maxY = 0.0f;
    float centerX = 0.0f, centerZ = 0.0f;
    uint32_t sampled = 0;
    double sumX = 0.0, sumZ = 0.0;

    for (uint32_t m = 0; m < model->meshCount; m++) {
        Mesh *mesh = &model->meshes[m];
        if (!mesh->vertices || mesh->vertexCount == 0) {
            continue;
        }
        for (uint32_t i = 0; i < mesh->vertexCount; i++) {
            const glm::vec3 &p = mesh->vertices[i].position;
            if (sampled == 0) {
                minY = maxY = p.y;
            } else {
                if (p.y < minY) minY = p.y;
                if (p.y > maxY) maxY = p.y;
            }
            sumX += p.x;
            sumZ += p.z;
            sampled++;
        }
    }

    if (sampled == 0) {
        return;
    }

    float height = maxY - minY;
    float scale = (height > 1e-6f) ? (targetHeight / height) : 1.0f;
    centerX = (float)(sumX / sampled);
    centerZ = (float)(sumZ / sampled);
    float centerY = (minY + maxY) * 0.5f;

    for (uint32_t m = 0; m < model->meshCount; m++) {
        Mesh *mesh = &model->meshes[m];
        if (!mesh->vertices) {
            continue;
        }
        for (uint32_t i = 0; i < mesh->vertexCount; i++) {
            Vertex *v = &mesh->vertices[i];
            v->position = (v->position - glm::vec3(centerX, centerY, centerZ)) * scale;
            /* Normals/tangents are directions: translate would not affect
             * them but the uniform scale does, so renormalize. */
            v->normal = glm::normalize(v->normal);
            v->tangent = glm::normalize(v->tangent);
            v->bitangent = glm::normalize(v->bitangent);
        }
    }

    /* Move the skeleton to match, so the pose still lines up with the mesh. */
    glm::mat4 xform = glm::translate(glm::mat4(1.0f), -glm::vec3(centerX, centerY, centerZ));
    for (uint32_t b = 0; b < model->boneCount; b++) {
        model->boneOffset[b] = glm::scale(xform, glm::vec3(scale)) * model->boneOffset[b];
    }

    /* Some exporters (this FBX among them) keep mesh vertices in world
     * space while the bone hierarchy also carries that world offset, so
     * applying world * inverseBind to a vertex pushes it further away
     * instead of leaving it alone.
     *
     * A bone's bind-pose world matrix is exactly inverse(inverseBind). At
     * rest the pose equals the bind world, so
     * world * inverseBind should be the identity and leave vertices
     * untouched — but only when the bone's rest *local* transform already
     * matches its bind world. Exporters that inflate the local transforms
     * with a world offset break that.
     *
     * Correction: corr = bindWorld * inverse(restWorld), so that
     *   inverse(corr) * restWorld == bindWorld
     * and folding inverse(corr) into the inverse bind matrix makes the rest
     * pose an exact identity while leaving real animation intact. */
    if (model->boneCount > 0) {
        glm::mat4 *restWorld = (glm::mat4 *)malloc(model->boneCount * sizeof(glm::mat4));
        if (restWorld) {
            for (uint32_t b = 0; b < model->boneCount; b++) {
                restWorld[b] = (model->boneParent[b] >= 0)
                             ? restWorld[model->boneParent[b]] * model->boneOffset[b]
                             : model->boneOffset[b];
            }
            for (uint32_t b = 0; b < model->boneCount; b++) {
                glm::mat4 bindWorld = glm::inverse(model->boneInverseBind[b]);
                glm::mat4 corr = bindWorld * glm::inverse(restWorld[b]);
                model->boneInverseBind[b] = glm::inverse(corr) * model->boneInverseBind[b];
            }
            free(restWorld);        }
    }

    printf("  normalized: scale=%.4f height=%.3f\n", scale, height);
}

int model_loadtextures(Model *model, const char *modelDir)
{
    if (!model || !model->meshes || model->meshCount == 0) {
        return 0;
    }

    /* Slot 0 belongs to the font atlas, so model textures start at 1.
     * MAX_BINDLESS_RESOURCES in the graphics backends caps the array. */
    int slot = 1;
    int loaded = 0;

    for (uint32_t i = 0; i < model->meshCount; i++) {
        Mesh *mesh = &model->meshes[i];
        mesh->diffuseTexture = NULL;
        mesh->diffuseTextureSlot = -1;

        if (!mesh->material.diffuseTexture || slot >= 64) {
            continue;
        }

        char full[4096];
        if (modelDir && modelDir[0]) {
            snprintf(full, sizeof(full), "%s/%s", modelDir, mesh->material.diffuseTexture);
        } else {
            snprintf(full, sizeof(full), "%s", mesh->material.diffuseTexture);
        }

        Texture tex = image_load(full);
        if (!tex) {
            /* Textures are optional; a missing file should not fail the load. */
            continue;
        }

        mesh->diffuseTexture = (void *)tex;
        mesh->diffuseTextureSlot = slot++;
        loaded++;
    }

    if (loaded > 0) {
        printf("  loaded %d material texture(s)\n", loaded);
    }
    return loaded;
}

int model_pose_by_index(Model *model, int clipIndex, float time, float *out16)
{
    if (!model || !out16 || model->boneCount == 0) {
        return 0;
    }

    const ModelAnimation *clip = NULL;
    if (clipIndex >= 0 && (uint32_t)clipIndex < model->animationCount) {
        clip = &model->animations[clipIndex];
    }

    /* Reuse the mat4 path, then flatten into the plain float array the
     * graphics layer expects (column-major, matching GLM). */
    glm::mat4 *tmp = (glm::mat4 *)malloc(model->boneCount * sizeof(glm::mat4));
    if (!tmp) {
        return 0;
    }

    int ok = model_pose(model, clip, time, tmp);
    if (ok) {
        for (uint32_t i = 0; i < model->boneCount; i++) {
            memcpy(out16 + (size_t)i * 16, &tmp[i], 16 * sizeof(float));
        }
    }
    free(tmp);
    return ok;
}

uint32_t model_get_mesh_count(const Model *model)
{
    if (!model) return 0;
    return model->meshCount;
}

uint32_t model_get_bone_count(const Model *model)
{
    if (!model) return 0;
    return model->boneCount;
}

uint32_t model_get_animation_count(const Model *model)
{
    if (!model) return 0;
    return model->animationCount;
}

const ModelAnimation *model_get_animation(const Model *model, uint32_t index)
{
    if (!model || index >= model->animationCount) return NULL;
    return &model->animations[index];
}

int model_get_animation_info(const Model *model, uint32_t index, ModelAnimationInfo *out)
{
    if (!model || !out || index >= model->animationCount) return 0;
    const ModelAnimation *clip = &model->animations[index];
    out->name = clip->name;
    out->duration = clip->duration;
    out->keyCount = clip->keyCount;
    return 1;
}

int model_is_skinned(const Model *model)
{
    return model && model->boneCount > 0;
}

/* Flatten the hierarchy: world[i] = world[parent] * offset[i] */
static void model_accumulate(Model *model, glm::mat4 *world)
{
    for (uint32_t i = 0; i < model->boneCount; i++) {
        world[i] = (model->boneParent[i] >= 0)
                 ? world[model->boneParent[i]] * model->boneOffset[i]
                 : model->boneOffset[i];
    }
}

int model_pose(Model *model, const ModelAnimation *clip, float time,
               glm::mat4 *out)
{
    if (!model || !out || model->boneCount == 0) {
        return 0;
    }

    /* Bones are not guaranteed to be ordered parent-before-child, so
     * repeatedly resolve any bone whose parent is already done. */
    glm::mat4 *world = (glm::mat4 *)malloc(model->boneCount * sizeof(glm::mat4));
    glm::mat4 *local = (glm::mat4 *)malloc(model->boneCount * sizeof(glm::mat4));
    char *done = (char *)calloc(model->boneCount, 1);
    if (!world || !local || !done) {
        free(world);
        free(local);
        free(done);
        return 0;
    }

    /* Sample the clip: nearest-key is enough here and avoids blending
     * bookkeeping; linear interpolation between the two surrounding keys
     * would be smoother but needs per-component slerp/lerp. */
    float t = clip ? time : 0.0f;
    if (clip && clip->duration > 0.0f && clip->frames) {
        if (t < 0.0f) t = 0.0f;
        if (t > clip->duration) t = clip->duration;
    }

    for (uint32_t i = 0; i < model->boneCount; i++) {
        if (clip && clip->frames && clip->keyCount > 0) {
            float framePos = (clip->duration > 0.0f)
                           ? (t / clip->duration) * (clip->keyCount - 1)
                           : 0.0f;
            uint32_t f0 = (uint32_t)framePos;
            uint32_t f1 = (f0 + 1 < clip->keyCount) ? f0 + 1 : f0;
            if (f0 >= clip->keyCount) f0 = clip->keyCount - 1;
            float frac = framePos - (float)f0;
            const glm::mat4 &a = clip->frames[(size_t)i * clip->keyCount + f0];
            const glm::mat4 &b = clip->frames[(size_t)i * clip->keyCount + f1];
            /* Interpolate each column. Adequate for the TRS tracks Assimp
             * emits; a proper decomposition into T/R/S and slerp would be
             * smoother but is not needed to get motion on screen. */
            local[i] = glm::mat4(
                a[0] + (b[0] - a[0]) * frac,
                a[1] + (b[1] - a[1]) * frac,
                a[2] + (b[2] - a[2]) * frac,
                a[3] + (b[3] - a[3]) * frac);
        } else {
            local[i] = model->boneOffset[i];
        }
    }

    /* Resolve in dependency order. model->boneParent is small (tens), so a
     * simple repeated pass is fine and avoids a depth-first recursion. */
    uint32_t remaining = model->boneCount;
    while (remaining > 0) {
        int progress = 0;
        for (uint32_t i = 0; i < model->boneCount; i++) {
            if (done[i]) continue;
            int parent = model->boneParent[i];
            if (parent >= 0 && !done[parent]) continue;

            world[i] = (parent >= 0) ? world[parent] * local[i] : local[i];
            out[i] = world[i] * model->boneInverseBind[i];
            done[i] = 1;
            remaining--;
            progress = 1;
        }
        if (!progress) {
            /* Cycle in the parent chain (corrupt file): fall back to rest. */
            fprintf(stderr, "model_pose: bone hierarchy has a cycle\n");
            for (uint32_t i = 0; i < model->boneCount; i++) {
                if (!done[i]) {
                    out[i] = model->boneOffset[i] * model->boneInverseBind[i];
                    done[i] = 1;
                }
            }
            break;
        }
    }

    free(world);
    free(local);
    free(done);
    return 1;
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

    free(model->boneOffset);
    free(model->boneInverseBind);
    free(model->boneParent);

    if (model->animations) {
        for (uint32_t i = 0; i < model->animationCount; i++) {
            free(model->animations[i].name);
            free(model->animations[i].frames);
        }
        free(model->animations);
    }

    if (model->name) {
        free(model->name);
    }

    free(model);
}
