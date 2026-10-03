/* Copyright Planimeter. All Rights Reserved. */

#include "tilemap.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Floats per engine Vertex: position(3) normal(3) uv(2) tangent(3)
 * bitangent(3) boneWeights(4) boneIndices(4). */
#define TILEMAP_VERTEX_FLOATS 22

/* The map is always square and centred on the origin, so the world's bounds
 * and the character controller's clamp agree without extra bookkeeping. */
#define TILEMAP_RESOLUTION 128

/* Peak height of the landscape. Must stay in step with
 * PHYSICS_TERRAIN_AMPLITUDE in physics.c: the character walks on this surface,
 * so if the two disagree it walks on ground the camera does not see. */
#define TILEMAP_AMPLITUDE 7.0f

/* Hash-based value noise, matching terrain.c so a given seed reproduces the
 * same landscape shape before it was stepped into tiles. Local rather than
 * shared because terrain.h's noise is not part of tilemap.h's surface. */
static uint32_t tilemap_hash(int32_t x, int32_t z, uint32_t seed)
{
    uint32_t h = seed;
    h ^= (uint32_t)x * 0x8da6b343u;
    h ^= (uint32_t)z * 0xd8163841u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

static float tilemap_lattice(int32_t x, int32_t z, uint32_t seed)
{
    return ((float)(tilemap_hash(x, z, seed) >> 8) / (float)(1 << 23)) - 1.0f;
}

static float tilemap_fade(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

static float tilemap_noise(float x, float z, uint32_t seed)
{
    float fx = floorf(x);
    float fz = floorf(z);
    int32_t ix = (int32_t)fx;
    int32_t iz = (int32_t)fz;
    float tx = tilemap_fade(x - fx);
    float tz = tilemap_fade(z - fz);

    float v00 = tilemap_lattice(ix,     iz,     seed);
    float v10 = tilemap_lattice(ix + 1, iz,     seed);
    float v01 = tilemap_lattice(ix,     iz + 1, seed);
    float v11 = tilemap_lattice(ix + 1, iz + 1, seed);

    float a = v00 + (v10 - v00) * tx;
    float b = v01 + (v11 - v01) * tx;
    return a + (b - a) * tz;
}

/* Height of one tile. This is the single authority for both the mesh and the
 * collision query, so what is drawn and what is stood on cannot disagree.
 *
 * The noise is sampled at the tile's own lattice point, not at the query
 * position, which is what makes the height constant across a tile and stepped
 * between neighbours — a ramp instead would just be the smooth heightfield
 * again. */
float tilemap_height_at(const TileMapConfig *config, float x, float z)
{
    if (!config || config->size <= 0.0f) {
        return 0.0f;
    }

    float size = config->size;
    float half = size * (float)TILEMAP_RESOLUTION * 0.5f;

    /* Clamp to the map so a query outside it lands on an edge tile rather
     * than sampling noise from nowhere. */
    float cx = x;
    float cz = z;
    float limit = half - size * 0.5f;
    if (cx < -limit) cx = -limit;
    if (cx >  limit) cx =  limit;
    if (cz < -limit) cz = -limit;
    if (cz >  limit) cz =  limit;

    /* Tile coordinates, taken at the tile's centre so the whole tile gets one
     * height. */
    int ix = (int)floorf((cx + half) / size);
    int iz = (int)floorf((cz + half) / size);

    float sum = 0.0f;
    float freq = 0.045f;
    float amp = 1.0f;
    for (int o = 0; o < 4; o++) {
        sum += tilemap_noise((float)ix * freq * size, (float)iz * freq * size,
                             config->seed + (uint32_t)o * 7919u) * amp;
        freq *= 2.0f;
        amp *= 0.5f;
    }

    return sum * TILEMAP_AMPLITUDE;
}

int tilemap_index_at(const TileMapConfig *config, float x, float z,
                     int tilesPerSide)
{
    if (!config || config->size <= 0.0f || tilesPerSide <= 0) {
        return 0;
    }

    float size = config->size;
    float half = size * (float)tilesPerSide * 0.5f;
    float limit = half - size * 0.5f;

    float cx = x < -limit ? -limit : (x > limit ? limit : x);
    float cz = z < -limit ? -limit : (z > limit ? limit : z);

    int ix = (int)floorf((cx + half) / size);
    int iz = (int)floorf((cz + half) / size);
    if (ix < 0) ix = 0;
    if (ix >= tilesPerSide) ix = tilesPerSide - 1;
    if (iz < 0) iz = 0;
    if (iz >= tilesPerSide) iz = tilesPerSide - 1;

    return iz * tilesPerSide + ix;
}

void *tilemap_build_mesh(const TileMapConfig *config)
{
    if (!config || config->size <= 0.0f) {
        return NULL;
    }

    float size = config->size;
    int res = TILEMAP_RESOLUTION;
    int sub = (int)config->resolution;
    if (sub < 1) sub = 1;

    int vertsPerSide = res * sub;
    uint32_t vertexCount = (uint32_t)(vertsPerSide * vertsPerSide);
    uint32_t quadCount = (uint32_t)((vertsPerSide - 1) * (vertsPerSide - 1));
    uint32_t indexCount = quadCount * 6;

    size_t vertexFloats = (size_t)vertexCount * TILEMAP_VERTEX_FLOATS;
    size_t vertexBytes = vertexFloats * sizeof(float);
    size_t indexBytes = (size_t)indexCount * sizeof(uint32_t);

    /* One allocation: vertices then indices, indices 4-byte aligned, which is
     * the layout terrain.c uses and graphics_createmesh() expects. */
    unsigned char *block = (unsigned char *)calloc(1, vertexBytes + indexBytes);
    if (!block) {
        return NULL;
    }

    float *verts = (float *)block;
    uint32_t *indices = (uint32_t *)(block + vertexBytes);

    float half = size * (float)res * 0.5f;
    float step = size / (float)sub;

    for (int j = 0; j < vertsPerSide; j++) {
        for (int i = 0; i < vertsPerSide; i++) {
            float x = -half + (float)i * step;
            float z = -half + (float)j * step;
            float h = tilemap_height_at(config, x, z);

            float *v = &verts[((size_t)j * vertsPerSide + i) * TILEMAP_VERTEX_FLOATS];
            v[0] = x;  v[1] = h;  v[2] = z;              /* position */
            v[3] = 0.0f; v[4] = 1.0f; v[5] = 0.0f;      /* normal, +Y */
            /* UVs in world units, not 0..1 across the map. A single 0..1 span
             * would stretch one texel over 512 units and read as a flat wash,
             * which is exactly what a green plane looks like. Samplers are set
             * to repeat, so world-unit UVs tile the ground texture at a fixed
             * physical size instead. */
            v[6] = x;
            v[7] = z;
            v[8] = 1.0f; v[9] = 0.0f; v[10] = 0.0f;      /* tangent +X */
            v[11] = 0.0f; v[12] = 0.0f; v[13] = 1.0f;    /* bitangent +Z */
            v[14] = 1.0f; v[15] = 0.0f; v[16] = 0.0f; v[17] = 0.0f;
        }
    }

    uint32_t n = 0;
    for (int j = 0; j < vertsPerSide - 1; j++) {
        for (int i = 0; i < vertsPerSide - 1; i++) {
            uint32_t a = (uint32_t)(j * vertsPerSide + i);
            uint32_t b = a + 1;
            uint32_t c = a + (uint32_t)vertsPerSide;
            uint32_t d = c + 1;
            /* Wound so front faces point up (+Y) under back-face culling. */
            indices[n++] = a; indices[n++] = c; indices[n++] = b;
            indices[n++] = b; indices[n++] = c; indices[n++] = d;
        }
    }

    return block;
}

void tilemap_free_mesh(void *mesh)
{
    free(mesh);
}
