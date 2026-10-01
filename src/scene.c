/* Copyright Planimeter. All Rights Reserved. */

#include "scene.h"
#include "math_c.h"
#include <string.h>

static int scene_valid(const Scene *scene, int node)
{
    return scene && node >= 0 && node < SCENE_MAX_NODES && scene->nodes[node].active;
}

void scene_init(Scene *scene)
{
    if (!scene) return;
    memset(scene, 0, sizeof(*scene));
    for (int i = 0; i < SCENE_MAX_NODES; i++) {
        scene->nodes[i].parent = -1;
        scene->nodes[i].active = 0;
    }
}

int scene_create(Scene *scene, int parent)
{
    if (!scene) return -1;
    if (parent >= 0 && !scene_valid(scene, parent)) return -1;

    for (int i = 0; i < SCENE_MAX_NODES; i++) {
        SceneNode *n = &scene->nodes[i];
        if (n->active) continue;

        n->parent = parent;
        n->position[0] = n->position[1] = n->position[2] = 0.0f;
        n->rotation[0] = n->rotation[1] = n->rotation[2] = 0.0f;
        n->scale[0] = n->scale[1] = n->scale[2] = 1.0f;
        math_identity(n->world);
        n->active = 1;
        return i;
    }
    return -1;
}

int scene_setparent(Scene *scene, int node, int parent)
{
    if (!scene_valid(scene, node)) return 0;
    if (parent >= 0) {
        if (!scene_valid(scene, parent)) return 0;
        /* Reject cycles: walk up from the proposed parent */
        int walk = parent;
        for (int depth = 0; depth < SCENE_MAX_NODES && walk >= 0; depth++) {
            if (walk == node) return 0;
            walk = scene->nodes[walk].parent;
        }
    }
    scene->nodes[node].parent = parent;
    return 1;
}

void scene_setposition(Scene *scene, int node, float x, float y, float z)
{
    if (!scene_valid(scene, node)) return;
    scene->nodes[node].position[0] = x;
    scene->nodes[node].position[1] = y;
    scene->nodes[node].position[2] = z;
}

void scene_setrotation(Scene *scene, int node, float x, float y, float z)
{
    if (!scene_valid(scene, node)) return;
    scene->nodes[node].rotation[0] = x;
    scene->nodes[node].rotation[1] = y;
    scene->nodes[node].rotation[2] = z;
}

void scene_setscale(Scene *scene, int node, float x, float y, float z)
{
    if (!scene_valid(scene, node)) return;
    scene->nodes[node].scale[0] = x;
    scene->nodes[node].scale[1] = y;
    scene->nodes[node].scale[2] = z;
}

static void scene_resolve(Scene *scene, int node, unsigned char *done, int depth)
{
    if (node < 0 || node >= SCENE_MAX_NODES || done[node] || depth >= SCENE_MAX_NODES) {
        return;
    }
    done[node] = 1;

    SceneNode *n = &scene->nodes[node];

    float local[16];
    math_identity(local);
    math_translate(local, local, n->position[0], n->position[1], n->position[2]);
    math_rotate(local, local, n->rotation[0], 1.0f, 0.0f, 0.0f);
    math_rotate(local, local, n->rotation[1], 0.0f, 1.0f, 0.0f);
    math_rotate(local, local, n->rotation[2], 0.0f, 0.0f, 1.0f);
    math_scale(local, local, n->scale[0], n->scale[1], n->scale[2]);

    if (n->parent >= 0) {
        scene_resolve(scene, n->parent, done, depth + 1);
        math_multiply(n->world, scene->nodes[n->parent].world, local);
    } else {
        memcpy(n->world, local, sizeof(local));
    }
}

void scene_update(Scene *scene)
{
    if (!scene) return;

    unsigned char done[SCENE_MAX_NODES];
    memset(done, 0, sizeof(done));

    for (int i = 0; i < SCENE_MAX_NODES; i++) {
        if (scene->nodes[i].active) {
            scene_resolve(scene, i, done, 0);
        }
    }
}

const float *scene_world(const Scene *scene, int node)
{
    if (!scene_valid(scene, node)) return NULL;
    return scene->nodes[node].world;
}
