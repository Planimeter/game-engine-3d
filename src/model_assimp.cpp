/* Copyright Planimeter. All Rights Reserved. */

#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

/* Animation data structures (internal) */

typedef struct {
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;
    float time;
} NodeKeyframe;

typedef struct {
    NodeKeyframe *keyframes;
    uint32_t keyframeCount;
    char *nodeName;
} AnimationChannel;

typedef struct {
    AnimationChannel *channels;
    uint32_t channelCount;
    float duration;
    float ticksPerSecond;
    char *name;
} AnimationClip;

typedef struct {
    char *name;
    int parentIndex;
    glm::mat4 localTransform;
    int boneIndex;
} SceneNode;

/* ---- Forward declarations ---- */
static Mesh *model_processmesh(const struct aiMesh *aiMesh, const struct aiScene *scene);
static MaterialData model_processmaterial(const struct aiMaterial *aiMat);
static int model_find_bone(Model *model, const char *name);
static int model_find_node(SceneNode *nodes, uint32_t count, const char *name);

static int model_find_bone(Model *model, const char *name)
{
    if (!model || !name) return -1;
    for (uint32_t i = 0; i < model->boneCount; i++) {
        if (model->boneNames[i] && strcmp(model->boneNames[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int model_find_node(SceneNode *nodes, uint32_t count, const char *name)
{
    if (!nodes || !name) return -1;
    for (uint32_t i = 0; i < count; i++) {
        if (nodes[i].name && strcmp(nodes[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static float model_find_keyframe_index(const NodeKeyframe *keyframes, uint32_t count, float time, float *fraction)
{
    if (count == 0) {
        *fraction = 0.0f;
        return 0;
    }
    if (count == 1 || time <= keyframes[0].time) {
        *fraction = 0.0f;
        return 0;
    }
    if (time >= keyframes[count - 1].time) {
        *fraction = 0.0f;
        return count - 1;
    }
    for (uint32_t i = 0; i < count - 1; i++) {
        if (time >= keyframes[i].time && time < keyframes[i + 1].time) {
            float range = keyframes[i + 1].time - keyframes[i].time;
            *fraction = (range > 0.0f) ? (time - keyframes[i].time) / range : 0.0f;
            return i;
        }
    }
    *fraction = 0.0f;
    return count - 1;
}

static glm::vec3 model_lerp_vec3(const glm::vec3 &a, const glm::vec3 &b, float t)
{
    return a + t * (b - a);
}

static void model_sample_channel(const AnimationChannel *channel, float time,
                                  glm::vec3 &pos, glm::quat &rot, glm::vec3 &scl)
{
    pos = channel->keyframes[0].position;
    rot = channel->keyframes[0].rotation;
    scl = channel->keyframes[0].scale;

    for (uint32_t i = 0; i < channel->keyframeCount - 1; i++) {
        if (time >= channel->keyframes[i].time && time < channel->keyframes[i + 1].time) {
            float range = channel->keyframes[i + 1].time - channel->keyframes[i].time;
            float frac = (range > 0.0f) ? (time - channel->keyframes[i].time) / range : 0.0f;
            pos = model_lerp_vec3(channel->keyframes[i].position, channel->keyframes[i + 1].position, frac);
            rot = glm::slerp(channel->keyframes[i].rotation, channel->keyframes[i + 1].rotation, frac);
            scl = model_lerp_vec3(channel->keyframes[i].scale, channel->keyframes[i + 1].scale, frac);
            return;
        }
    }
}

static void model_build_node_transforms(Model *model, int nodeIndex, const glm::mat4 &parentTransform)
{
    SceneNode *nodes = (SceneNode *)model->sceneNodes;
    if (!nodes || nodeIndex < 0 || (uint32_t)nodeIndex >= model->sceneNodeCount) {
        return;
    }

    SceneNode *node = &nodes[nodeIndex];

    AnimationClip *clips = (AnimationClip *)model->animationData;
    if (clips && model->animationIndex < model->animationCount) {
        AnimationClip *clip = &clips[model->animationIndex];
        for (uint32_t c = 0; c < clip->channelCount; c++) {
            if (clip->channels[c].nodeName && node->name &&
                strcmp(clip->channels[c].nodeName, node->name) == 0) {
                glm::vec3 pos, scl;
                glm::quat rot;
                model_sample_channel(&clip->channels[c], model->animationTime, pos, rot, scl);

                glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
                glm::mat4 R = glm::mat4_cast(rot);
                glm::mat4 S = glm::scale(glm::mat4(1.0f), scl);
                node->localTransform = T * R * S;
                break;
            }
        }
    }

    glm::mat4 globalTransform = parentTransform * node->localTransform;

    if (node->boneIndex >= 0 && (uint32_t)node->boneIndex < model->boneCount) {
        model->boneTransforms[node->boneIndex] = globalTransform * model->boneOffsets[node->boneIndex];
    }

    for (uint32_t i = 0; i < model->sceneNodeCount; i++) {
        if (nodes[i].parentIndex == (int)nodeIndex) {
            model_build_node_transforms(model, i, globalTransform);
        }
    }
}

static void model_free_clips(Model *model)
{
    AnimationClip *clips = (AnimationClip *)model->animationData;
    if (!clips) return;

    for (uint32_t a = 0; a < model->animationCount; a++) {
        for (uint32_t c = 0; c < clips[a].channelCount; c++) {
            free(clips[a].channels[c].keyframes);
            free(clips[a].channels[c].nodeName);
        }
        free(clips[a].channels);
        free(clips[a].name);
    }
    free(clips);
    model->animationData = NULL;
}

static void model_free_nodes(Model *model)
{
    SceneNode *nodes = (SceneNode *)model->sceneNodes;
    if (!nodes) return;

    for (uint32_t i = 0; i < model->sceneNodeCount; i++) {
        free(nodes[i].name);
    }
    free(nodes);
    model->sceneNodes = NULL;
}

static Mesh *model_processmesh(const struct aiMesh *aiMesh, const struct aiScene *scene)
{
    Mesh *mesh = (Mesh *)malloc(sizeof(Mesh));
    if (!mesh) {
        fprintf(stderr, "Failed to allocate memory for mesh\n");
        return NULL;
    }
    memset(mesh, 0, sizeof(Mesh));

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

        /* Initialize bone data to no-bone defaults */
        mesh->vertices[i].boneIDs = glm::uvec4(0);
        mesh->vertices[i].boneWeights = glm::vec4(0.0f);
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

    /* Process bone data */
    if (aiMesh->HasBones()) {
        mesh->boneCount = aiMesh->mNumBones;

        /* Temporary per-vertex bone accumulators */
        uint32_t *tempBoneCounts = (uint32_t *)calloc(mesh->vertexCount, sizeof(uint32_t));
        uint32_t **tempBoneIDs = (uint32_t **)calloc(mesh->vertexCount, sizeof(uint32_t *));
        float **tempBoneWeights = (float **)calloc(mesh->vertexCount, sizeof(float *));

        if (!tempBoneCounts || !tempBoneIDs || !tempBoneWeights) {
            free(tempBoneCounts);
            free(tempBoneIDs);
            free(tempBoneWeights);
            goto bone_cleanup;
        }

        for (uint32_t i = 0; i < mesh->vertexCount; i++) {
            tempBoneIDs[i] = (uint32_t *)malloc(MAX_BONES_PER_VERTEX * sizeof(uint32_t));
            tempBoneWeights[i] = (float *)malloc(MAX_BONES_PER_VERTEX * sizeof(float));
            if (!tempBoneIDs[i] || !tempBoneWeights[i]) {
                goto bone_cleanup;
            }
        }

        for (uint32_t b = 0; b < aiMesh->mNumBones; b++) {
            const struct aiBone *bone = aiMesh->mBones[b];
            for (uint32_t w = 0; w < bone->mNumWeights; w++) {
                uint32_t vid = bone->mWeights[w].mVertexId;
                float weight = bone->mWeights[w].mWeight;
                if (vid < mesh->vertexCount && tempBoneCounts[vid] < MAX_BONES_PER_VERTEX) {
                    uint32_t idx = tempBoneCounts[vid];
                    tempBoneIDs[vid][idx] = b;
                    tempBoneWeights[vid][idx] = weight;
                    tempBoneCounts[vid]++;
                }
            }
        }

        /* Pack into vertex attributes */
        for (uint32_t i = 0; i < mesh->vertexCount; i++) {
            uint32_t count = tempBoneCounts[i];
            glm::uvec4 ids(0);
            glm::vec4 weights(0.0f);
            for (uint32_t j = 0; j < count && j < MAX_BONES_PER_VERTEX; j++) {
                ids[j] = tempBoneIDs[i][j];
                weights[j] = tempBoneWeights[i][j];
            }
            /* Normalize weights */
            float sum = weights.x + weights.y + weights.z + weights.w;
            if (sum > 0.0f) {
                weights /= sum;
            }
            mesh->vertices[i].boneIDs = ids;
            mesh->vertices[i].boneWeights = weights;
        }

bone_cleanup:
        for (uint32_t i = 0; i < mesh->vertexCount; i++) {
            free(tempBoneIDs ? tempBoneIDs[i] : NULL);
            free(tempBoneWeights ? tempBoneWeights[i] : NULL);
        }
        free(tempBoneCounts);
        free(tempBoneIDs);
        free(tempBoneWeights);
    } else {
        mesh->boneCount = 0;
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
    memset(model, 0, sizeof(Model));

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

    /* ---- Process bone data ---- */
    {
        uint32_t totalBones = 0;
        for (uint32_t m = 0; m < scene->mNumMeshes; m++) {
            const struct aiMesh *aiMesh = scene->mMeshes[m];
            if (aiMesh->HasBones()) {
                totalBones += aiMesh->mNumBones;
            }
        }

        if (totalBones > 0) {
            model->boneCount = 0;
            model->boneOffsets = (glm::mat4 *)malloc(totalBones * sizeof(glm::mat4));
            model->boneNames = (char **)malloc(totalBones * sizeof(char *));
            model->boneTransforms = (glm::mat4 *)calloc(totalBones, sizeof(glm::mat4));
            if (!model->boneOffsets || !model->boneNames || !model->boneTransforms) {
                fprintf(stderr, "Failed to allocate bone data\n");
                free(model->boneOffsets);
                free(model->boneNames);
                free(model->boneTransforms);
                model->boneOffsets = NULL;
                model->boneNames = NULL;
                model->boneTransforms = NULL;
                model->boneCount = 0;
            } else {
                for (uint32_t m = 0; m < scene->mNumMeshes; m++) {
                    const struct aiMesh *aiMesh = scene->mMeshes[m];
                    if (!aiMesh->HasBones()) continue;

                    for (uint32_t b = 0; b < aiMesh->mNumBones; b++) {
                        const struct aiBone *bone = aiMesh->mBones[b];

                        int existing = model_find_bone(model, bone->mName.data);
                        if (existing >= 0) {
                            continue;
                        }

                        uint32_t idx = model->boneCount;
                        model->boneOffsets[idx] = glm::transpose(glm::make_mat4(&bone->mOffsetMatrix.a1));
                        model->boneNames[idx] = (char *)malloc(bone->mName.length + 1);
                        if (model->boneNames[idx]) {
                            strcpy(model->boneNames[idx], bone->mName.data);
                        }
                        model->boneCount++;
                    }
                }

                printf("Model '%s': %u bones\n", filepath, model->boneCount);

                for (uint32_t m = 0; m < model->meshCount; m++) {
                    Mesh *mesh = &model->meshes[m];
                    if (!scene->mMeshes[m]->HasBones()) continue;

                    for (uint32_t b = 0; b < scene->mMeshes[m]->mNumBones; b++) {
                        const struct aiBone *aiBone = scene->mMeshes[m]->mBones[b];
                        int boneIdx = model_find_bone(model, aiBone->mName.data);
                        if (boneIdx < 0) continue;

                        for (uint32_t w = 0; w < aiBone->mNumWeights; w++) {
                            uint32_t vid = aiBone->mWeights[w].mVertexId;
                            if (vid < mesh->vertexCount) {
                                glm::uvec4 &ids = mesh->vertices[vid].boneIDs;
                                for (int j = 0; j < MAX_BONES_PER_VERTEX; j++) {
                                    if (ids[j] == (uint32_t)b && mesh->vertices[vid].boneWeights[j] > 0.0f) {
                                        ids[j] = (uint32_t)boneIdx;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    /* ---- Build scene node hierarchy ---- */
    {
        /* Count all nodes recursively */
        uint32_t nodeCount = 0;
        struct aiNode *nodeStack[256];
        int stackTop = 0;
        nodeStack[stackTop++] = scene->mRootNode;
        while (stackTop > 0) {
            struct aiNode *current = nodeStack[--stackTop];
            nodeCount++;
            for (uint32_t i = 0; i < current->mNumChildren; i++) {
                if (stackTop < 256) {
                    nodeStack[stackTop++] = current->mChildren[i];
                }
            }
        }

        model->sceneNodeCount = nodeCount;
        model->sceneNodes = (SceneNode *)calloc(nodeCount, sizeof(SceneNode));
        SceneNode *nodes = (SceneNode *)model->sceneNodes;

        /* Build nodes in breadth-first order */
        struct aiNode *bfsQueue[256];
        int bfsHead = 0, bfsTail = 0;
        bfsQueue[bfsTail++] = scene->mRootNode;
        uint32_t writeIdx = 0;

        while (bfsHead < bfsTail && writeIdx < nodeCount) {
            struct aiNode *current = bfsQueue[bfsHead++];

            nodes[writeIdx].name = (char *)malloc(current->mName.length + 1);
            if (nodes[writeIdx].name) {
                strcpy(nodes[writeIdx].name, current->mName.data);
            }
            nodes[writeIdx].localTransform = glm::transpose(glm::make_mat4(&current->mTransformation.a1));
            nodes[writeIdx].boneIndex = model_find_bone(model, current->mName.data);

            /* Find parent index */
            if (current->mParent) {
                for (uint32_t p = 0; p < writeIdx; p++) {
                    if (nodes[p].name && strcmp(nodes[p].name, current->mParent->mName.data) == 0) {
                        nodes[writeIdx].parentIndex = (int)p;
                        break;
                    }
                }
            } else {
                nodes[writeIdx].parentIndex = -1;
            }

            writeIdx++;

            for (uint32_t i = 0; i < current->mNumChildren; i++) {
                if (bfsTail < 256) {
                    bfsQueue[bfsTail++] = current->mChildren[i];
                }
            }
        }
    }

    /* ---- Process animation clips (copy keyframes from Assimp) ---- */
    model->animationCount = scene->mNumAnimations;
    if (model->animationCount > 0) {
        model->animationNames = (char **)malloc(model->animationCount * sizeof(char *));
        model->animationData = malloc(model->animationCount * sizeof(AnimationClip));
        AnimationClip *clips = (AnimationClip *)model->animationData;
        model->animationDuration = 0.0f;
        model->animationTime = 0.0f;
        model->animationIndex = 0;

        for (uint32_t a = 0; a < scene->mNumAnimations; a++) {
            const struct aiAnimation *anim = scene->mAnimations[a];

            /* Animation name */
            if (anim->mName.length > 0) {
                model->animationNames[a] = (char *)malloc(anim->mName.length + 1);
                if (model->animationNames[a]) {
                    strcpy(model->animationNames[a], anim->mName.data);
                }
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "Animation_%u", a);
                model->animationNames[a] = (char *)malloc(strlen(buf) + 1);
                if (model->animationNames[a]) {
                    strcpy(model->animationNames[a], buf);
                }
            }

            float tps = (float)anim->mTicksPerSecond;
            if (tps <= 0.0f) tps = 25.0f;
            clips[a].ticksPerSecond = tps;
            clips[a].duration = (float)anim->mDuration / tps;

            if (clips[a].duration > model->animationDuration) {
                model->animationDuration = clips[a].duration;
            }

            /* Clip name */
            clips[a].name = model->animationNames[a];

            /* Copy channels */
            clips[a].channelCount = anim->mNumChannels;
            clips[a].channels = (AnimationChannel *)calloc(anim->mNumChannels, sizeof(AnimationChannel));

            for (uint32_t c = 0; c < anim->mNumChannels; c++) {
                const struct aiNodeAnim *chan = anim->mChannels[c];

                clips[a].channels[c].nodeName = (char *)malloc(chan->mNodeName.length + 1);
                if (clips[a].channels[c].nodeName) {
                    strcpy(clips[a].channels[c].nodeName, chan->mNodeName.data);
                }

                /* Use the maximum keyframe count across position/rotation/scale */
                uint32_t maxKeys = chan->mNumPositionKeys;
                if (chan->mNumRotationKeys > maxKeys) maxKeys = chan->mNumRotationKeys;
                if (chan->mNumScalingKeys > maxKeys) maxKeys = chan->mNumScalingKeys;

                clips[a].channels[c].keyframeCount = maxKeys;
                clips[a].channels[c].keyframes = (NodeKeyframe *)malloc(maxKeys * sizeof(NodeKeyframe));

                for (uint32_t k = 0; k < maxKeys; k++) {
                    NodeKeyframe &kf = clips[a].channels[c].keyframes[k];

                    /* Position */
                    if (k < chan->mNumPositionKeys) {
                        kf.position = glm::vec3(chan->mPositionKeys[k].mValue.x,
                                                 chan->mPositionKeys[k].mValue.y,
                                                 chan->mPositionKeys[k].mValue.z);
                        kf.time = (float)chan->mPositionKeys[k].mTime / tps;
                    } else if (chan->mNumPositionKeys > 0) {
                        kf.position = glm::vec3(chan->mPositionKeys[chan->mNumPositionKeys - 1].mValue.x,
                                                 chan->mPositionKeys[chan->mNumPositionKeys - 1].mValue.y,
                                                 chan->mPositionKeys[chan->mNumPositionKeys - 1].mValue.z);
                        kf.time = (float)chan->mPositionKeys[chan->mNumPositionKeys - 1].mTime / tps;
                    }

                    /* Rotation */
                    if (k < chan->mNumRotationKeys) {
                        kf.rotation = glm::quat(chan->mRotationKeys[k].mValue.w,
                                                 chan->mRotationKeys[k].mValue.x,
                                                 chan->mRotationKeys[k].mValue.y,
                                                 chan->mRotationKeys[k].mValue.z);
                    } else if (chan->mNumRotationKeys > 0) {
                        kf.rotation = glm::quat(chan->mRotationKeys[chan->mNumRotationKeys - 1].mValue.w,
                                                 chan->mRotationKeys[chan->mNumRotationKeys - 1].mValue.x,
                                                 chan->mRotationKeys[chan->mNumRotationKeys - 1].mValue.y,
                                                 chan->mRotationKeys[chan->mNumRotationKeys - 1].mValue.z);
                    }

                    /* Scale */
                    if (k < chan->mNumScalingKeys) {
                        kf.scale = glm::vec3(chan->mScalingKeys[k].mValue.x,
                                              chan->mScalingKeys[k].mValue.y,
                                              chan->mScalingKeys[k].mValue.z);
                    } else if (chan->mNumScalingKeys > 0) {
                        kf.scale = glm::vec3(chan->mScalingKeys[chan->mNumScalingKeys - 1].mValue.x,
                                              chan->mScalingKeys[chan->mNumScalingKeys - 1].mValue.y,
                                              chan->mScalingKeys[chan->mNumScalingKeys - 1].mValue.z);
                    }
                }

                printf("  Animation '%s' channel '%s': %u keyframes\n",
                       clips[a].name ? clips[a].name : "?",
                       clips[a].channels[c].nodeName ? clips[a].channels[c].nodeName : "?",
                       maxKeys);
            }

            printf("  Animation '%s': %.2fs, %u channels\n",
                   model->animationNames[a], clips[a].duration, clips[a].channelCount);
        }
    }

    aiReleaseImport(scene);

    printf("Model loaded: %s with %d meshes\n", filepath, model->meshCount);

    return model;
}

uint32_t model_get_mesh_count(const Model *model)
{
    if (!model) return 0;
    return model->meshCount;
}

void model_update(Model *model, float deltaTime)
{
    if (!model || model->animationCount == 0 || !model->boneTransforms) return;

    model->animationTime += deltaTime;
    if (model->animationDuration > 0.0f) {
        while (model->animationTime >= model->animationDuration) {
            model->animationTime -= model->animationDuration;
        }
    }

    for (uint32_t i = 0; i < model->boneCount; i++) {
        model->boneTransforms[i] = glm::mat4(1.0f);
    }

    SceneNode *nodes = (SceneNode *)model->sceneNodes;
    if (nodes) {
        for (uint32_t i = 0; i < model->sceneNodeCount; i++) {
            if (nodes[i].parentIndex == -1) {
                model_build_node_transforms(model, i, glm::mat4(1.0f));
            }
        }
    }
}

const float *model_get_bone_matrices(const Model *model)
{
    if (!model || !model->boneTransforms) return NULL;
    return (const float *)model->boneTransforms;
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

void model_set_animation(Model *model, uint32_t index)
{
    if (!model || index >= model->animationCount) return;
    model->animationIndex = index;
    model->animationTime = 0.0f;
}

const char *model_get_animation_name(const Model *model, uint32_t index)
{
    if (!model || index >= model->animationCount) return NULL;
    return model->animationNames[index];
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

    /* Free animation data */
    if (model->boneOffsets) {
        free(model->boneOffsets);
    }
    if (model->boneTransforms) {
        free(model->boneTransforms);
    }
    if (model->boneNames) {
        for (uint32_t i = 0; i < model->boneCount; i++) {
            free(model->boneNames[i]);
        }
        free(model->boneNames);
    }
    if (model->animationNames) {
        for (uint32_t i = 0; i < model->animationCount; i++) {
            free(model->animationNames[i]);
        }
        free(model->animationNames);
    }
    model_free_clips(model);
    model_free_nodes(model);

    if (model->name) {
        free(model->name);
    }

    free(model);
}
