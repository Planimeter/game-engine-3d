/* Copyright Planimeter. All Rights Reserved. */

#ifndef SCENE_H
#define SCENE_H

#ifdef __cplusplus
extern "C" {
#endif

#define SCENE_MAX_NODES 64

/* Fixed-pool scene graph node. Local transform is TRS; the world matrix
 * is recomputed by scene_update() with parents resolved first. */
typedef struct {
    int parent;         /* index of parent node, -1 for a root */
    float position[3];
    float rotation[3];  /* euler radians, applied X then Y then Z */
    float scale[3];
    float world[16];    /* parent world * translate * rotate * scale */
    int active;
} SceneNode;

typedef struct {
    SceneNode nodes[SCENE_MAX_NODES];
} Scene;

/* Mark every slot inactive. */
void scene_init(Scene *scene);

/* Claim an inactive slot. parent is a node handle or -1 for a root.
 * Returns the new handle, or -1 if the pool is full or parent is invalid. */
int scene_create(Scene *scene, int parent);

/* Reparent a node; rejects cycles and invalid handles. Returns 1 on success. */
int scene_setparent(Scene *scene, int node, int parent);

void scene_setposition(Scene *scene, int node, float x, float y, float z);
void scene_setrotation(Scene *scene, int node, float x, float y, float z);
void scene_setscale(Scene *scene, int node, float x, float y, float z);

/* Recompute world matrices for every active node (parents before children). */
void scene_update(Scene *scene);

/* World matrix of an active node, or NULL if the handle is inactive. */
const float *scene_world(const Scene *scene, int node);

#ifdef __cplusplus
}
#endif

#endif /* SCENE_H */
