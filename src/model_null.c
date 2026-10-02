/* Copyright Planimeter. All Rights Reserved. */

#include "model.h"

Model *model_load(const char *filepath)
{
    return NULL;
}

void model_destroy(Model *model)
{
}

uint32_t model_get_mesh_count(const Model *model)
{
    return 0;
}

uint32_t model_get_bone_count(const Model *model)
{
    return 0;
}

uint32_t model_get_animation_count(const Model *model)
{
    return 0;
}

const ModelAnimation *model_get_animation(const Model *model, uint32_t index)
{
    return NULL;
}

int model_get_animation_info(const Model *model, uint32_t index, ModelAnimationInfo *out)
{
    return 0;
}

int model_is_skinned(const Model *model)
{
    return 0;
}

int model_pose(Model *model, const ModelAnimation *clip, float time, glm::mat4 *out)
{
    return 0;
}

void model_normalize(Model *model, float targetHeight)
{
}

int model_pose_by_index(Model *model, int clipIndex, float time, float *out16)
{
    return 0;
}
