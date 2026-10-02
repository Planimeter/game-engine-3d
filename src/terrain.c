/* Copyright Planimeter. All Rights Reserved. */

#include "terrain.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Hash-based value noise. Integer lattice hashing rather than a permutation
 * table keeps this self-contained and identical across platforms. */
static uint32_t terrain_hash(int32_t x, int32_t z, uint32_t seed)
{
    uint32_t h = seed;
    h ^= (uint32_t)x * 0x8da6b343u;
    h ^= (uint32_t)z * 0xd8163841u;
    /* splitmix-style finalizer: avalanches nearby lattice points apart */
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

static float terrain_lattice(int32_t x, int32_t z, uint32_t seed)
{
    /* Map to [-1, 1] so octaves sum to zero-mean and the terrain has both
     * hills and valleys rather than drifting upward. */
    return ((float)(terrain_hash(x, z, seed) >> 8) / (float)(1 << 23)) - 1.0f;
}

/* Smoothstep fade, so lattice values interpolate without visible creases. */
static float terrain_fade(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

float terrain_noise(float x, float z, uint32_t seed)
{
    float fx = floorf(x);
    float fz = floorf(z);
    int32_t ix = (int32_t)fx;
    int32_t iz = (int32_t)fz;
    float tx = terrain_fade(x - fx);
    float tz = terrain_fade(z - fz);

    float v00 = terrain_lattice(ix,     iz,     seed);
    float v10 = terrain_lattice(ix + 1, iz,     seed);
    float v01 = terrain_lattice(ix,     iz + 1, seed);
    float v11 = terrain_lattice(ix + 1, iz + 1, seed);

    float a = v00 + (v10 - v00) * tx;
    float b = v01 + (v11 - v01) * tx;
    return a + (b - a) * tz;
}

float terrain_height(float x, float z, uint32_t seed)
{
    /* Four octaves at halving amplitude and doubling frequency: the low
     * octaves shape the overall hills, the high ones add surface detail.
     * Summed amplitude is ~1.87x the base, which the caller scales. */
    float sum = 0.0f;
    float freq = 0.045f;
    float amp = 1.0f;
    for (int o = 0; o < 4; o++) {
        sum += terrain_noise(x * freq, z * freq, seed + (uint32_t)o * 7919u) * amp;
        freq *= 2.0f;
        amp *= 0.5f;
    }
    return sum;
}

/* Floats per engine Vertex: position(3) normal(3) uv(2) tangent(3)
 * bitangent(3) boneWeights(4) boneIndices(4). Terrain is unskinned, so the
 * skin fields are left at bone 0 / weight 1, which is the identity. */
#define TERRAIN_VERTEX_FLOATS 22
#define TERRAIN_IDENTITY_SKIN(v) do { \
    (v)[14] = 1.0f; (v)[15] = 0.0f; (v)[16] = 0.0f; (v)[17] = 0.0f; \
} while (0)

void *terrain_build_mesh(uint32_t resolution, float extent, float amplitude,
                         uint32_t seed)
{
    if (resolution < 2) {
        return NULL;
    }

    uint32_t side = resolution * resolution;
    uint32_t indexCount = (resolution - 1) * (resolution - 1) * 6;
    size_t vertexBytes = (size_t)side * TERRAIN_VERTEX_FLOATS * sizeof(float);
    size_t indexBytes = (size_t)indexCount * sizeof(uint32_t);

    /* Vertices then indices in one allocation, indices 4-byte aligned. */
    unsigned char *block = (unsigned char *)calloc(1, vertexBytes + indexBytes);
    if (!block) {
        return NULL;
    }

    float *verts = (float *)block;
    uint32_t *indices = (uint32_t *)(block + vertexBytes);
    float step = extent / (float)(resolution - 1);
    float half = extent * 0.5f;

    for (uint32_t j = 0; j < resolution; j++) {
        for (uint32_t i = 0; i < resolution; i++) {
            float x = -half + (float)i * step;
            float z = -half + (float)j * step;
            float h = terrain_height(x, z, seed) * amplitude;

            float *v = verts + (size_t)(j * resolution + i) * TERRAIN_VERTEX_FLOATS;
            v[0] = x;  v[1] = h;  v[2] = z;              /* position */
            v[3] = 0.0f; v[4] = 1.0f; v[5] = 0.0f;      /* normal (refined below) */
            v[6] = (float)i / (float)(resolution - 1); /* uv */
            v[7] = (float)j / (float)(resolution - 1);
            v[8] = 1.0f; v[9] = 0.0f; v[10] = 0.0f;     /* tangent +X */
            v[11] = 0.0f; v[12] = 0.0f; v[13] = 1.0f;   /* bitangent +Z */
            TERRAIN_IDENTITY_SKIN(v);
        }
    }

    uint32_t n = 0;
    for (uint32_t j = 0; j < resolution - 1; j++) {
        for (uint32_t i = 0; i < resolution - 1; i++) {
            uint32_t a = j * resolution + i;
            uint32_t b = a + 1;
            uint32_t c = a + resolution;
            uint32_t d = c + 1;
            /* Wound so front faces point up (+Y) under back-face culling. */
            indices[n++] = a; indices[n++] = c; indices[n++] = b;
            indices[n++] = b; indices[n++] = c; indices[n++] = d;
        }
    }

    /* Normals from neighbouring samples. Edges fall back to a one-sided
     * difference rather than reading outside the grid. */
    float eps = step;
    for (uint32_t j = 0; j < resolution; j++) {
        for (uint32_t i = 0; i < resolution; i++) {
            size_t self = (size_t)(j * resolution + i) * TERRAIN_VERTEX_FLOATS;
            float hC = verts[self + 1];
            float hL = (i > 0)
                     ? verts[self - TERRAIN_VERTEX_FLOATS + 1] : hC - eps;
            float hR = (i + 1 < resolution)
                     ? verts[self + TERRAIN_VERTEX_FLOATS + 1] : hC + eps;
            float hD = (j > 0)
                     ? verts[self - (size_t)resolution * TERRAIN_VERTEX_FLOATS + 1] : hC - eps;
            float hU = (j + 1 < resolution)
                     ? verts[self + (size_t)resolution * TERRAIN_VERTEX_FLOATS + 1] : hC + eps;

            float nx = -(hR - hL);
            float ny = 2.0f * eps;
            float nz = -(hU - hD);
            float len = sqrtf(nx * nx + ny * ny + nz * nz);
            if (len > 1e-6f) {
                verts[self + 3] = nx / len;
                verts[self + 4] = ny / len;
                verts[self + 5] = nz / len;
            } else {
                verts[self + 3] = 0.0f;
                verts[self + 4] = 1.0f;
                verts[self + 5] = 0.0f;
            }
        }
    }

    return block;
}

void terrain_free_mesh(void *mesh)
{
    free(mesh);
}
