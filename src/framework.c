/* Copyright Planimeter. All Rights Reserved. */

#include "filesystem.h"
#include "window.h"
#include "graphics.h"
#include "audio.h"
#include "footstep.h"
#include "selftest.h"
#include "font.h"
#include "image.h"
#include "text.h"
#include "model.h"
#include "math_c.h"
#include "camera.h"
#include "physics.h"
#include "player.h"
#include "scene.h"
#include "terrain.h"
#include "tilemap.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <direct.h>
#define realpath(rel, abs) _fullpath(abs, rel, 4096)
#endif

static Font *g_testFont = NULL;
static Text *g_testText = NULL;
static uint64_t g_accumMs = 0;
static uint32_t g_frameCount = 0;
static int g_displayFps = 0;
static const uint64_t g_fpsUpdateIntervalMs = 1000;

/* 3D test model */
static Model *g_testModel = NULL;
static Material g_litMaterial = NULL;
static float g_elapsed = 0.0f;

/* Instanced grid drawn with a single graphics_draw_instanced() call */
#define INSTANCED_GRID_SIZE 10
#define INSTANCED_GRID_COUNT (INSTANCED_GRID_SIZE * INSTANCED_GRID_SIZE)
static Material g_gridMaterial = NULL;

/* Animation. g_animIndex < 0 means rest pose. The bone matrix array is
 * sized for the engine's 64-bone shader limit and reallocated if a loaded
 * model turns out to need more. */
#define MAX_SKIN_BONES 64
static float g_boneMatrices[MAX_SKIN_BONES * 16];
static int g_animIndex = -1;
static ModelAnimationInfo g_animInfo = {NULL, 0.0f, 0};

/* Tiled world. A grid of flat-topped blocks on a lattice rather than a smooth
 * heightfield: the blocky skyline is the readable, deliberately low-detail look
 * this style wants, and a lattice gives an exact ground height that collision
 * and the camera can both read, so a tile step cannot show up as the view
 * hovering over or sinking into the ground.
 *
 * 128 tiles of 4 units is a 512-unit map, which is what TERRAIN_EXTENT means
 * for the character's bounds clamp. */
#define TERRAIN_TILES 128
#define TERRAIN_TILE_SIZE 4.0f
#define TERRAIN_EXTENT (TERRAIN_TILES * TERRAIN_TILE_SIZE)
#define TERRAIN_AMPLITUDE 9.0f   /* read by physics_terrain_height's signature */
#define TERRAIN_SEED 20261002u
static Model *g_terrain = NULL;
static Material g_terrainMaterial = NULL;
/* Ground texture, tiled across the terrain through world-unit UVs. */
static Texture g_groundTexture = NULL;

/* Sky. An inward-facing dome centred on the camera, shaded by a vertical
 * gradient texture. It is ordinary geometry drawn with the existing
 * graphics API rather than a new engine entry point: the only special part
 * is the model matrix, which cancels the camera translation so the dome
 * never gets closer as the player walks, and a fixed vertex colour that
 * lifts the sky above the flat clear colour. */
#define SKY_RADIUS 900.0f     /* inside the terrain's far plane */
#define SKY_SEGMENTS 32       /* longitude divisions */
#define SKY_RINGS 16          /* latitude divisions */
static Model *g_sky = NULL;
static Material g_skyMaterial = NULL;
static Texture g_skyGradient = NULL;

static void framework_create_sky(void);
static void framework_create_footsteps(void);
static void framework_update_sky_gradient(void);
static float framework_sun_elevation(void);
static void framework_set_look(int active);

/* Day/night. One cycle drives everything time-of-day: the sky gradient's
 * colours and the direction every lit material shades from. A single sun
 * vector is the source of truth, so the landscape cannot end up lit from
 * one side while the sky shows the sun somewhere else.
 *
 * The cycle is a fixed period, not a wall clock, so the sky is reproducible
 * and the world stays fixed rather than drifting with real time. */
#define DAY_LENGTH_SECONDS 240.0f
#define SUN_TILT 0.35f        /* radians off vertical; 0 puts the sun overhead */
/* Start mid-morning: the sun is up, the sky is blue, and the first thing on
 * screen is daylight rather than a black landscape. */
static float g_timeOfDay = 0.34f; /* fraction of a cycle: 0 midnight, 0.5 noon */
static float g_sunDirection[3] = {0.0f, 1.0f, 0.0f};
/* Forces one gradient upload on the first update after the texture exists. */
static int g_skyGradientJustCreated = 1;

/* Controls overlay. Text objects are allocated once and rewritten each frame
 * by text_set(), which avoids per-frame allocation in the render loop. */
#define HELP_LINE_COUNT 3
static Text *g_helpText[HELP_LINE_COUNT];

/* Footsteps. Four variants so consecutive steps differ, plus a heavier
 * landing for the end of a jump. Missing files are tolerated: the system
 * plays whatever loaded. */
static FootstepSystem *g_footsteps = NULL;
static int g_wasGrounded = 1;

/* In-game self test. Opt-in via GAME_SELFTEST=1: it drives the character
 * through a scripted sequence and reports on screen, so the controller is
 * verified on the real terrain inside the real window rather than against a
 * synthetic map in a separate harness. Off by default because it takes over
 * the camera. */
static SelfTest g_selftest;
static int g_selftestActive = 0;

static void framework_create_footsteps(void)
{
    static const char *const stepNames[] = {
        "Sounds/step1.wav",
        "Sounds/step2.wav",
        "Sounds/step3.wav",
        "Sounds/step4.wav",
    };
    g_footsteps = footstep_create(stepNames,
                                  (int)(sizeof(stepNames) / sizeof(stepNames[0])),
                                  "Sounds/land.wav");
}

/* Camera + scene graph */
static Camera g_camera;
static Scene g_scene;
static int g_rootNode = -1;

/* Colliders for the scene props. The cubes are the only solid objects in
 * the world, and their transforms are fixed, so the boxes are resolved once
 * at load and handed to the character controller every frame. */
#define FRAMEWORK_MAX_PROPS 8
static PhysicsBox g_propBoxes[FRAMEWORK_MAX_PROPS];
static int g_propNodes[FRAMEWORK_MAX_PROPS];
static int g_propCount = 0;
static void framework_build_prop_boxes(void);

/* Character controller. The world is fixed; this is what moves in it. The
 * camera follows the character's eyes and owns nothing but orientation. */
static Player g_player;

/* Camera control input state (see framework_keypressed/keyreleased) */
static int g_keyForward = 0;
static int g_keyBack = 0;
static int g_keyLeft = 0;
static int g_keyRight = 0;
static int g_keyJump = 0;
static int g_keySprint = 0;
static int g_lookActive = 0;
static const float g_lookSensitivity = 0.0025f; /* radians per pixel */

/* Releases everything framework_load created. Registered before
 * graphics_init()'s own atexit so it runs first — graphics_shutdown()
 * destroys the device, and anything still holding GPU buffers at that point
 * is reported as leaked. */
static void framework_shutdown(void)
{
    if (g_testText) {
        text_destroy(g_testText);
        g_testText = NULL;
    }
    for (int i = 0; i < HELP_LINE_COUNT; i++) {
        if (g_helpText[i]) {
            text_destroy(g_helpText[i]);
            g_helpText[i] = NULL;
        }
    }
    if (g_testFont) {
        font_destroy(g_testFont);
        g_testFont = NULL;
    }
    if (g_testModel) {
        graphics_destroymodel(g_testModel);
        g_testModel = NULL;
    }
    if (g_litMaterial) {
        graphics_destroymaterial(g_litMaterial);
        g_litMaterial = NULL;
    }
    if (g_gridMaterial) {
        graphics_destroymaterial(g_gridMaterial);
        g_gridMaterial = NULL;
    }
    if (g_sky) {
        graphics_destroymodel(g_sky);
        g_sky = NULL;
    }
    if (g_skyMaterial) {
        graphics_destroymaterial(g_skyMaterial);
        g_skyMaterial = NULL;
    }
    if (g_terrain) {
        graphics_destroymodel(g_terrain);
        g_terrain = NULL;
    }
    if (g_footsteps) {
        footstep_destroy(g_footsteps);
        g_footsteps = NULL;
    }
    if (g_groundTexture) {
        image_free(g_groundTexture);
        g_groundTexture = NULL;
    }
    if (g_skyGradient) {
        graphics_destroytexture(g_skyGradient);
        g_skyGradient = NULL;
    }
}

/* Sky gradient as a 1x64 texture sampled by dome height. The dome's V runs
 * 1 at the zenith to 0 at the horizon, and a texture's row 0 lands at V = 0,
 * so the rows are written horizon-first.
 *
 * The palette is keyed to the sun's height: daylight reads blue overhead with
 * a pale horizon, dawn and dusk push orange into the low band, and night
 * falls to a near-black dome. Colours are interpolated between four times of
 * day and then written once, so the update is a small texture upload rather
 * than a per-fragment branch.
 *
 * Stops bunch toward the bottom because most of the visible sky is the band
 * just above the horizon. */
typedef struct {
    float t;   /* fraction of the cycle */
    float zenith[3];
    float horizon[3];
} SkyPalette;

static const SkyPalette SKY_PALETTE[] = {
    /* midnight */
    {0.00f, {0.02f, 0.03f, 0.08f}, {0.05f, 0.06f, 0.12f}},
    /* first light */
    {0.21f, {0.10f, 0.14f, 0.34f}, {0.62f, 0.42f, 0.34f}},
    /* morning */
    {0.30f, {0.16f, 0.34f, 0.70f}, {0.74f, 0.83f, 0.92f}},
    /* noon */
    {0.50f, {0.16f, 0.34f, 0.70f}, {0.74f, 0.83f, 0.92f}},
    /* evening */
    {0.70f, {0.20f, 0.26f, 0.56f}, {0.92f, 0.62f, 0.40f}},
    /* dusk */
    {0.79f, {0.09f, 0.11f, 0.28f}, {0.66f, 0.36f, 0.30f}},
    /* midnight again, so the cycle wraps cleanly */
    {1.00f, {0.02f, 0.03f, 0.08f}, {0.05f, 0.06f, 0.12f}},
};
#define SKY_PALETTE_COUNT ((int)(sizeof(SKY_PALETTE) / sizeof(SKY_PALETTE[0])))

static void framework_sample_sky_palette(float zenith[3], float horizon[3])
{
    /* Wrap into [0, 1) so the last entry is only ever reached as the wrap
     * point from 0.79 to 0.0. */
    float t = g_timeOfDay;
    t = t - floorf(t);

    int hi = 0;
    while (hi + 1 < SKY_PALETTE_COUNT - 1 && SKY_PALETTE[hi + 1].t < t) {
        hi++;
    }
    int lo = hi;
    if (SKY_PALETTE[hi].t > t && hi > 0) {
        lo = hi - 1;
    }

    float span = SKY_PALETTE[hi].t - SKY_PALETTE[lo].t;
    float f = span > 1e-6f ? (t - SKY_PALETTE[lo].t) / span : 0.0f;

    for (int c = 0; c < 3; c++) {
        zenith[c] = SKY_PALETTE[lo].zenith[c] +
                    (SKY_PALETTE[hi].zenith[c] - SKY_PALETTE[lo].zenith[c]) * f;
        horizon[c] = SKY_PALETTE[lo].horizon[c] +
                    (SKY_PALETTE[hi].horizon[c] - SKY_PALETTE[lo].horizon[c]) * f;
    }
}

/* Sun direction on the celestial sphere, tilted so the sun does not pass
 * straight overhead. `dir` points from the world toward the sun.
 *
 * The quarter-cycle offset puts sunrise at t = 0.25: the sun has to be on the
 * horizon there, so the angle is measured from sunrise, not from midnight.
 * Without the offset the sun sits a quarter cycle out of phase with the sky
 * palette, and the sky darkens at the moment the sun is highest. */
static void framework_compute_sun(float dir[3])
{
    float angle = (g_timeOfDay - 0.25f) * 2.0f * 3.14159265f;
    float x = cosf(angle) * SUN_TILT * 3.0f;
    float y = sinf(angle);
    float z = cosf(angle) * 0.45f;

    float len = sqrtf(x * x + y * y + z * z);
    if (len > 1e-6f) {
        x /= len;
        y /= len;
        z /= len;
    }

    dir[0] = x;
    dir[1] = y;
    dir[2] = z;
}

/* How high the sun is, 0 on the horizon and 1 overhead. Fades to 0 below the
 * horizon so dusk glow stops at the skyline. */
static float framework_sun_elevation(void)
{
    framework_compute_sun(g_sunDirection);
    return g_sunDirection[1] > 0.0f ? g_sunDirection[1] : 0.0f;
}


static void framework_update_sky_gradient(void)
{
    if (!g_skyGradient) {
        return;
    }

    enum { SKY_GRADIENT_H = 64 };
    unsigned char pixels[SKY_GRADIENT_H * 4];

    float zenith[3];
    float horizon[3];
    framework_sample_sky_palette(zenith, horizon);

    /* Sun glow near the skyline, strongest while the sun is low. Below the
     * horizon there is no sun to see. */
    float sunUp = framework_sun_elevation();

    for (int y = 0; y < SKY_GRADIENT_H; y++) {
        /* Row 0 is the horizon, matching V = 0 on the dome. */
        float v = (float)y / (float)(SKY_GRADIENT_H - 1);

        /* Blend horizon -> zenith with a curve that keeps most of the range
         * near the bottom, then add glow that falls off with height. */
        float blend = v * v * (3.0f - 2.0f * v) * 0.55f + v * 0.45f;
        float glow = (1.0f - v) * (1.0f - v) * sunUp * 0.55f;

        for (int c = 0; c < 3; c++) {
            float color = horizon[c] + (zenith[c] - horizon[c]) * blend;
            /* Warm the low band toward the sun colour. */
            color += glow * (c == 0 ? 1.0f : (c == 1 ? 0.62f : 0.28f));
            pixels[y * 4 + c] = (unsigned char)(255.0f * (color < 0.0f ? 0.0f :
                                                (color > 1.0f ? 1.0f : color)));
        }
        pixels[y * 4 + 3] = 255;
    }

    graphics_updatetexture(g_skyGradient, 0, 0, 1, SKY_GRADIENT_H, pixels);
}

static void framework_create_sky_gradient(void)
{
    /* Created at its full height, not 1x1. graphics_updatetexture() blits into
     * an existing texture and cannot resize one, so a texture made at 1x1 stays
     * 1x1 however many rows are written to it: the whole gradient collapses to
     * a single pixel, and sampling that one pixel across the dome gives a flat
     * white sky. The update below still writes every row on the first frame,
     * but it writes into a texture that is already the right size. */
    enum { SKY_GRADIENT_H = 64 };
    static const unsigned char empty[SKY_GRADIENT_H * 4] = {0};
    g_skyGradient = graphics_createtexture_rgba(1, SKY_GRADIENT_H, empty);
    framework_update_sky_gradient();
}

/* Build the dome and its material. Vertices are unit-sized so the model
 * matrix can carry both the radius and the camera-centred offset, and the
 * UV runs 1 at the zenith to 0 at the horizon to match the gradient rows.
 * Faces are wound so the inside is front-facing: the camera is inside. */
static void framework_create_sky(void)
{
    const int segments = SKY_SEGMENTS;
    const int rings = SKY_RINGS;
    /* One extra vertex at the nadir closes the dome into a sphere. A
     * hemisphere alone leaves a hole straight down, which shows as the sky
     * arcing overhead with terrain visible through the gap. */
    const int vertexCount = (rings + 1) * (segments + 1) + 1;
    const int indexCount = rings * segments * 6 + segments * 3;

    /* Same 22-float layout as the terrain mesh: position(3) normal(3)
     * uv(2) tangent(3) bitangent(3) boneWeights(4) boneIndices(4). */
    float *verts = (float *)calloc((size_t)vertexCount, 22 * sizeof(float));
    uint32_t *indices = (uint32_t *)calloc((size_t)indexCount, sizeof(uint32_t));
    if (!verts || !indices) {
        free(verts);
        free(indices);
        fprintf(stderr, "Failed to allocate sky dome\n");
        return;
    }

    for (int r = 0; r <= rings; r++) {
        /* theta 0 at the zenith, pi/2 at the horizon, where the terrain
         * takes over. */
        float t = (float)r / (float)rings;
        float theta = t * 3.14159265f * 0.5f;
        float sinT = sinf(theta);
        float cosT = cosf(theta);

        for (int s = 0; s <= segments; s++) {
            float phi = (float)s / (float)segments * 2.0f * 3.14159265f;
            float x = sinT * cosf(phi);
            float y = cosT;
            float z = sinT * sinf(phi);

            float *v = &verts[((size_t)r * (segments + 1) + s) * 22];
            v[0] = x;  v[1] = y;  v[2] = z;              /* position, unit */
            v[3] = -x; v[4] = -y; v[5] = -z;             /* inward normal */
            v[6] = (float)s / (float)segments;          /* u, unused */
            v[7] = 1.0f - t;                            /* v: 1 top, 0 horizon */
            v[8] = 1.0f; v[9] = 0.0f; v[10] = 0.0f;      /* tangent */
            v[11] = 0.0f; v[12] = 0.0f; v[13] = 1.0f;    /* bitangent */
            v[14] = 1.0f; v[15] = 0.0f; v[16] = 0.0f; v[17] = 0.0f;
        }
    }

    int n = 0;
    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < segments; s++) {
            int a = r * (segments + 1) + s;
            int b = a + 1;
            int c = a + segments + 1;
            int d = c + 1;
            /* Reversed winding versus the terrain grid, so the inward-facing
             * side survives back-face culling. */
            indices[n++] = (uint32_t)a; indices[n++] = (uint32_t)b; indices[n++] = (uint32_t)c;
            indices[n++] = (uint32_t)b; indices[n++] = (uint32_t)d; indices[n++] = (uint32_t)c;
        }
    }

    /* Close the bottom with a fan to a single nadir vertex. The cap sits
     * below the terrain and is only there so the view down is sky rather
     * than a hole; it samples the darkest row so it reads as shadowed
     * ground haze. */
    {
        int nadir = (rings + 1) * (segments + 1);
        float *v = &verts[(size_t)nadir * 22];
        v[0] = 0.0f; v[1] = -1.0f; v[2] = 0.0f;           /* straight down */
        v[3] = 0.0f; v[4] = 1.0f; v[5] = 0.0f;           /* inward */
        v[6] = 0.0f; v[7] = 0.0f;                       /* horizon row */
        v[8] = 1.0f; v[9] = 0.0f; v[10] = 0.0f;
        v[11] = 0.0f; v[12] = 0.0f; v[13] = 1.0f;
        v[14] = 1.0f; v[15] = 0.0f; v[16] = 0.0f; v[17] = 0.0f;

        for (int s = 0; s < segments; s++) {
            int a = rings * (segments + 1) + s;
            int b = a + 1;
            indices[n++] = (uint32_t)a; indices[n++] = (uint32_t)b; indices[n++] = (uint32_t)nadir;
        }
    }

    g_sky = graphics_createmesh(verts, (size_t)vertexCount, 22 * sizeof(float),
                                indices, (size_t)indexCount);
    free(verts);
    free(indices);
    if (!g_sky) {
        fprintf(stderr, "Failed to build sky dome\n");
        return;
    }

    framework_create_sky_gradient();
    /* Full ambient and no light contribution: the dome stands in for the
     * background, not for a lit surface, so shading it would only darken
     * the gradient. */
    g_skyMaterial = graphics_creatematerial(NULL);
    if (!g_skyMaterial) {
        return;
    }
    graphics_material_set_float(g_skyMaterial, "ambient", 1.0f);
    graphics_material_set_float(g_skyMaterial, "specular", 0.0f);
    graphics_material_set_float(g_skyMaterial, "shininess", 1.0f);
    graphics_material_set_float(g_skyMaterial, "normalSlot", -1.0f);
    graphics_material_set_vec3(g_skyMaterial, "lightDir", 0.0f, -1.0f, 0.0f);
    graphics_material_set_vec3(g_skyMaterial, "cameraPos", 0.0f, 0.0f, 0.0f);
    graphics_material_set_vec3(g_skyMaterial, "albedo", 1.0f, 1.0f, 1.0f);

    /* Bind the gradient as the dome's diffuse map. The default shader
     * multiplies albedo by the diffuse sample and by ambient, so a white
     * albedo at ambient 1 shows the gradient unmodified. */
    graphics_bindtexture(g_skyGradient, 1);
    graphics_material_set_float(g_skyMaterial, "diffuseSlot", 1.0f);
}

void framework_init(const char *argv0)
{
    filesystem_init(argv0);
    window_init();
    audio_init(argv0);
    /* Registered first, so it runs last: atexit is LIFO */
    atexit(framework_shutdown);
    graphics_init();
}

void framework_load(int argc, char *argv[])
{
    (void)argc;

#if defined(__APPLE__)
    /* Use PHYSFS-mounted system fonts */
    g_testFont = font_create("Fonts/Geneva.ttf", 16);
#elif defined(_WIN32)
    g_testFont = font_create("Fonts/segoeui.ttf", 16);
#else
    g_testFont = font_create("Fonts/segoeui.ttf", 16);
#endif

    if (g_testFont) {
        g_testText = text_create(g_testFont, "FPS: 0");
        for (int i = 0; i < HELP_LINE_COUNT; i++) {
            g_helpText[i] = text_create(g_testFont, "");
        }
    }

    /* Load 3D test model.
     * model_load() uses Assimp (real filesystem, not PHYSFS), so derive
     * the absolute path from argv[0] (the executable path). */
    if (argv && argv[0]) {
        char resolved[4096];
        if (realpath(argv[0], resolved)) {
            /* Navigate up from .../Contents/MacOS/game to repo root.
             * Path: build/game.app/Contents/MacOS/game — need to go up 5 levels. */
            char *p = strrchr(resolved, '/');
            if (p) {
                *p = '\0'; /* .../Contents/MacOS */
                for (int i = 0; i < 4; i++) { /* up 4 more: MacOS, Contents, game.app, build */
                    p = strrchr(resolved, '/');
                    if (p) *p = '\0';
                }
                char modelPath[4096];
                /* MODEL_OVERRIDE lets a different model (e.g. a skinned one)
                 * be loaded without editing this file. */
                const char *override = getenv("MODEL_OVERRIDE");
                snprintf(modelPath, sizeof(modelPath), "%s/Models/%s", resolved,
                         override ? override : "cube.obj");
                g_testModel = graphics_loadmodel(modelPath);
                if (g_testModel) {
                    printf("Loaded model from %s (%d meshes)\n",
                           modelPath, model_get_mesh_count(g_testModel));
                    if (model_is_skinned(g_testModel)) {
                        printf("  skinned: %u bones, %u animations\n",
                               model_get_bone_count(g_testModel),
                               model_get_animation_count(g_testModel));
                        ModelAnimationInfo info;
                        if (model_get_animation_info(g_testModel, 0, &info)) {
                            g_animIndex = 0;
                            g_animInfo = info;
                            printf("  clip '%s': %.3fs, %u keys/bone\n",
                                   info.name ? info.name : "(unnamed)",
                                   info.duration, info.keyCount);
                        }
                        /* Imported assets use wildly different scales and
                         * origins (this one spans ~35 units vertically and
                         * sits far from the origin). Normalize so the demo
                         * camera framing works for any model. */
                        model_normalize(g_testModel, 2.0f);
                        /* Vertex data changed after upload; push it back. */
                        graphics_reuploadmodel(g_testModel);
                    }

                    /* Material textures, resolved relative to the model's
                     * own directory. */
                    {
                        char dir[4096];
                        snprintf(dir, sizeof(dir), "%s", modelPath);
                        char *slash = strrchr(dir, '/');
                        if (slash) *slash = '\0';
                        model_loadtextures(g_testModel, dir);
                    }
                } else {
                    fprintf(stderr, "Failed to load model from %s\n", modelPath);
                }
            }
        }
        /* Fallback: try relative path */
        if (!g_testModel) {
            g_testModel = graphics_loadmodel("../Models/cube.obj");
            if (g_testModel) {
                printf("Loaded cube model from ../Models/cube.obj (%d meshes)\n",
                       model_get_mesh_count(g_testModel));
            }
        }
    }

    /* Tiled landscape. One vertex per tile corner at resolution 1, so the
     * surface is stepped and every tile is flat. */
    {
        TileMapConfig config;
        config.size = TERRAIN_TILE_SIZE;
        config.resolution = 1.0f;
        config.seed = TERRAIN_SEED;

        void *mesh = tilemap_build_mesh(&config);
        if (mesh) {
            uint32_t side = (uint32_t)TERRAIN_TILES * TERRAIN_TILES;
            uint32_t quads = (uint32_t)(TERRAIN_TILES - 1) * (TERRAIN_TILES - 1);
            /* tilemap_build_mesh lays vertices then indices in one block. */
            g_terrain = graphics_createmesh(
                mesh, side, 22 * sizeof(float),
                (const uint32_t *)(mesh + (size_t)side * 22 * sizeof(float)),
                (size_t)quads * 6);
            tilemap_free_mesh(mesh);
            if (g_terrain) {
                printf("Terrain: %dx%d tiles, %.0f units across, seed %u\n",
                       TERRAIN_TILES, TERRAIN_TILES,
                       TERRAIN_EXTENT, TERRAIN_SEED);
            }
        }
        if (!g_terrain) {
            fprintf(stderr, "Failed to build terrain\n");
        }
    }

    /* Resolve the sun before anything reads it: the sky gradient and every
     * lit material take their light direction from this one vector, so the
     * landscape cannot end up lit from a different angle than the sky. */
    framework_compute_sun(g_sunDirection);
    framework_create_sky();

    /* Ground texture. Loaded through image_load() so it goes through the same
     * stb_image path the rest of the engine uses, rather than embedding raw
     * pixels. */
    g_groundTexture = image_load("ground.png");
    if (!g_groundTexture) {
        fprintf(stderr, "Failed to load ground.png; terrain will be untextured\n");
    }

    framework_create_footsteps();

    /* Opt-in self test. The overlay reuses the three help text objects: there
     * are more results than objects, so the list scrolls and older results
     * drop off the top. That is enough to read a pass or a fail, which is the
     * point — this is a diagnostic, not a report to admire. */
    {
        const char *env = getenv("GAME_SELFTEST");
        if (env && env[0] == '1') {
            g_selftestActive = 1;
            selftest_begin(&g_selftest);
        }
    }

    /* Lit material: floats are ambient/specular/shininess, vec3s are
     * light direction/camera position/albedo (slot order matters — the
     * material API packs by slot, not by name). The model matrix is
     * updated per frame via graphics_material_set_mat4. */
    g_litMaterial = graphics_creatematerial(NULL);
    if (g_litMaterial) {
        graphics_material_set_float(g_litMaterial, "ambient", 0.15f);
        graphics_material_set_float(g_litMaterial, "specular", 0.45f);
        graphics_material_set_float(g_litMaterial, "shininess", 32.0f);
        /* floats[3]: diffuse texture slot. -1 until model_loadtextures()
         * finds a map; material packing reads the whole float block, so
         * leaving it unset would feed the shader garbage. */
        graphics_material_set_float(g_litMaterial, "diffuseSlot", -1.0f);
        graphics_material_set_float(g_litMaterial, "normalSlot", -1.0f);
        graphics_material_set_vec3(g_litMaterial, "lightDir", -g_sunDirection[0],
                                   -g_sunDirection[1], -g_sunDirection[2]);
        graphics_material_set_vec3(g_litMaterial, "cameraPos", 0.0f, 0.0f, 3.0f);
        graphics_material_set_vec3(g_litMaterial, "albedo", 0.85f, 0.55f, 0.25f);
    }

    /* Instanced grid material: slot order must match the setters, same as
     * g_litMaterial, but a different albedo so the grid reads as its own
     * pass. The model matrix is ignored by the instanced shader (each
     * instance carries its own), so it stays identity. */
    g_gridMaterial = graphics_creatematerial(NULL);
    if (g_gridMaterial) {
        graphics_material_set_float(g_gridMaterial, "ambient", 0.2f);
        graphics_material_set_float(g_gridMaterial, "specular", 0.5f);
        graphics_material_set_float(g_gridMaterial, "shininess", 48.0f);
        graphics_material_set_float(g_gridMaterial, "diffuseSlot", -1.0f);
        graphics_material_set_float(g_gridMaterial, "normalSlot", -1.0f);
        graphics_material_set_vec3(g_gridMaterial, "lightDir", -g_sunDirection[0],
                                   -g_sunDirection[1], -g_sunDirection[2]);
        graphics_material_set_vec3(g_gridMaterial, "cameraPos", 0.0f, 0.0f, 3.0f);
        graphics_material_set_vec3(g_gridMaterial, "albedo", 0.35f, 0.6f, 0.85f);
        static const float identity[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        graphics_material_set_mat4(g_gridMaterial, identity);
    }

    /* Terrain material: greener and rougher than the cubes so the landscape
     * reads as ground rather than another object. */
    g_terrainMaterial = graphics_creatematerial(NULL);
    if (g_terrainMaterial) {
        graphics_material_set_float(g_terrainMaterial, "ambient", 0.22f);
        graphics_material_set_float(g_terrainMaterial, "specular", 0.08f);
        graphics_material_set_float(g_terrainMaterial, "shininess", 8.0f);
        /* The ground texture goes in the ordinary diffuse slot. Without it the
         * terrain is one uniform albedo across 512 units, which reads as a flat
         * green plane no matter how the lighting is tuned. */
        graphics_material_set_float(g_terrainMaterial, "diffuseSlot", -1.0f);
        graphics_material_set_float(g_terrainMaterial, "normalSlot", -1.0f);
        graphics_material_set_vec3(g_terrainMaterial, "lightDir", -g_sunDirection[0],
                                   -g_sunDirection[1], -g_sunDirection[2]);
        graphics_material_set_vec3(g_terrainMaterial, "cameraPos", 0.0f, 0.0f, 3.0f);
        /* White, because the ground texture carries the colour. The shader
         * multiplies albedo by the diffuse sample, so a coloured albedo on top
         * of a coloured texture multiplies twice and the terrain comes out at
         * roughly a third of the brightness it should be. */
        graphics_material_set_vec3(g_terrainMaterial, "albedo", 1.0f, 1.0f, 1.0f);
    }

    /* Character starts on the surface well behind the scene cubes, so it
     * does not spawn inside the test model. The camera sits at its eyes. */
    player_init(&g_player, 0.0f, 14.0f, TERRAIN_SEED, TERRAIN_AMPLITUDE,
                TERRAIN_EXTENT);
    {
        float eye[3];
        player_eye(&g_player, eye);
        camera_init(&g_camera, eye[0], eye[1], eye[2]);
        camera_zoom(&g_camera, 6.0f * 0.12f);
        g_camera.pitch = 0.0f;
    }

    /* Scene graph: a static root with three children, so parent->child
     * transform composition is visible. Sited on the terrain so the cubes sit
     * in the landscape rather than floating. Each child is also a collider,
     * so the character cannot walk through what it can see. */
    scene_init(&g_scene);
    g_rootNode = scene_create(&g_scene, -1);
    if (g_rootNode >= 0) {
        int childLeft = scene_create(&g_scene, g_rootNode);
        int childRight = scene_create(&g_scene, g_rootNode);
        int childTop = scene_create(&g_scene, g_rootNode);

        /* Cubes sit on the terrain, sampled through the same helper the
         * character collides against, so a prop's collider box matches the
         * ground it rests on. */
        float baseY = physics_terrain_height(0.0f, 0.0f, TERRAIN_SEED, TERRAIN_AMPLITUDE);
        scene_setposition(&g_scene, childLeft, -1.3f, baseY + 0.5f, 0.0f);
        scene_setscale(&g_scene, childLeft, 0.5f, 0.5f, 0.5f);
        scene_setposition(&g_scene, childRight, 1.3f, baseY + 0.5f, 0.0f);
        scene_setscale(&g_scene, childRight, 0.5f, 0.5f, 0.5f);
        scene_setposition(&g_scene, childTop, 0.0f, baseY + 1.3f, 0.0f);
        scene_setscale(&g_scene, childTop, 0.5f, 0.5f, 0.5f);

        g_propNodes[0] = childLeft;
        g_propNodes[1] = childRight;
        g_propNodes[2] = childTop;
        g_propCount = 3;
    }

    framework_build_prop_boxes();
}

/* Scene nodes are static, so their collider boxes only need resolving once.
 * A node's world matrix is parent world * TRS; the cube mesh is a unit box,
 * so the world position is column 3 and the world scale is the length of
 * columns 0, 1 and 2. The root carries no scale and holds no collider. */
static void framework_build_prop_boxes(void)
{
    if (g_propCount <= 0) {
        return;
    }

    scene_update(&g_scene);
    for (int i = 0; i < g_propCount; i++) {
        const float *world = scene_world(&g_scene, g_propNodes[i]);
        if (!world) {
            g_propCount = i;
            return;
        }
        for (int a = 0; a < 3; a++) {
            float len = 0.0f;
            for (int r = 0; r < 3; r++) {
                float v = world[a * 4 + r];
                len += v * v;
            }
            g_propBoxes[i].center[a] = world[12 + a];
            /* The unit cube spans [-1, 1] before scaling, so the scaled
             * half-extent is half the world column length. */
            g_propBoxes[i].half[a] = 0.5f * sqrtf(len);
        }
    }
}

/* Clear held keys and mouse-look. Called from framework_mousefocus(false) and
 * exposed for the paths that deactivate the window without delivering a
 * focus event, so the character cannot keep walking with nobody at the keys. */
void framework_releaseinput(void)
{
    g_keyForward = 0;
    g_keyBack = 0;
    g_keyLeft = 0;
    g_keyRight = 0;
    g_keyJump = 0;
    g_keySprint = 0;
    g_lookActive = 0;
}

void framework_update(uint64_t deltaTime)
{
    g_accumMs += deltaTime;
    g_frameCount += 1;

    if (g_accumMs >= g_fpsUpdateIntervalMs) {
        g_displayFps = (int)((g_frameCount * 1000ULL) / g_accumMs);
        g_accumMs -= g_fpsUpdateIntervalMs;
        g_frameCount = 0;

        if (g_testText) {
            char fpsText[64];
            snprintf(fpsText, sizeof(fpsText), "FPS: %d", g_displayFps);
            text_set(g_testText, fpsText);
        }
    }

    float dt = (float)deltaTime / 1000.0f;
    g_elapsed += dt;

    /* Scene cubes stay put. They used to orbit the root at
     * g_elapsed * 0.5, which was the only motion on screen before the
     * terrain existed and read as the whole world rotating. */

    /* Advance the day. The cycle is a fixed period rather than a wall clock,
     * so the world stays fixed and the sky is reproducible. The gradient is
     * only re-uploaded when the palette has moved far enough to see, which
     * keeps this off the per-frame path. */
    {
        float before = g_timeOfDay;
        g_timeOfDay += dt / DAY_LENGTH_SECONDS;
        g_timeOfDay -= floorf(g_timeOfDay);

        /* Re-upload the gradient a few times a minute, not every frame.
         *
         * graphics_updatetexture() allocates a staging buffer and builds its
         * own MTLCommandQueue per call, so one per frame means ~119 command
         * queues a second — enough to stall the whole desktop, not just the
         * game. The cycle is 240s, so a 0.002 step is one update every half
         * second and the gradient moves less than a single row in between.
         * Without this the sky is still smooth; it is just not re-uploaded
         * 119 times for every visible change. */
        if (fabsf(g_timeOfDay - before) >= 0.002f ||
            g_skyGradientJustCreated) {
            framework_compute_sun(g_sunDirection);
            framework_update_sky_gradient();
            g_skyGradientJustCreated = 0;
        }
    }

    /* WASD drives the character along the ground; it never translates the
     * camera. E/Q are gone as free-flight: vertical motion is gravity and
     * the jump key, so the camera cannot be lifted out of the world. */
    PlayerInput input;
    memset(&input, 0, sizeof(input));
    input.forward = (g_keyForward ? 1.0f : 0.0f) - (g_keyBack ? 1.0f : 0.0f);
    input.right = (g_keyRight ? 1.0f : 0.0f) - (g_keyLeft ? 1.0f : 0.0f);
    input.jump = g_keyJump;
    input.sprint = g_keySprint;
    g_keyJump = 0;

    player_update(&g_player, &input, g_camera.yaw, dt,
                  g_propCount > 0 ? g_propBoxes : NULL, g_propCount);

    /* Footsteps follow horizontal speed, so the cadence tracks the walk or
     * sprint rather than running at a fixed rate, and a landing fires on the
     * frame the character touches back down. */
    if (g_footsteps) {
        float speed = sqrtf(g_player.velocity[0] * g_player.velocity[0] +
                            g_player.velocity[2] * g_player.velocity[2]);
        footstep_update(g_footsteps, speed, g_player.grounded, g_wasGrounded, dt);
        g_wasGrounded = g_player.grounded;
    }

    /* The self test drives the character itself, so it runs after the normal
     * input path and before the camera follows, which means the camera tracks
     * whatever the test is doing. */
    if (g_selftestActive) {
        selftest_update(&g_selftest, &g_player, &input, g_camera.yaw, dt,
                        g_propCount > 0 ? g_propBoxes : NULL, g_propCount);
    }

    /* Camera follows the character's eyes. Only orientation is its own, so
     * mouse-look turns the view without moving the player. */
    {
        float eye[3];
        player_eye(&g_player, eye);
        g_camera.position[0] = eye[0];
        g_camera.position[1] = eye[1];
        g_camera.position[2] = eye[2];
    }

    /* Advance the test model's animation without replacing the game update. */
    if (g_testModel) {
        model_update(g_testModel, (float)deltaTime / 1000.0f);
    }
}

void framework_draw(void)
{
    /* Draw 3D scene */
    if (g_testModel) {
        int w, h;
        window_getwindowsizeinpixels(&w, &h);
        float aspect = (float)w / (float)h;

        float proj[16], view[16], mvp[16], temp[16];

        /* Camera-driven projection and view. Reverse-Z with an infinite far
         * plane: precision concentrates near the camera, which the 600-unit
         * landscape needs. Both backends compare depth with GREATER and
         * clear to 0 to match. */
        camera_projection_reversez(&g_camera, aspect, proj);
        camera_view(&g_camera, view);

        /* Every lit surface shades from the same sun the sky is drawn with,
         * and the shader wants the direction light travels, so the world
         * toward the sun is negated here.
         *
         * Once the sun is below the horizon the direct term clamps to zero
         * and only ambient remains, so ambient needs a real floor or the
         * landscape turns to black at night. Albedo is tinted toward the
         * sky's night colour at the same time, which is what makes it read
         * as moonlight rather than as an unlit scene. */
        {
            float elevation = framework_sun_elevation();
            /* A floor high enough to read as moonlit ground rather than an
             * unlit scene: at night only this term survives, since the
             * direct term clamps to zero once the sun is down. */
            /* Exposure.
             *
             * The shader is albedo * (ambient + (1 - ambient) * ndl), so the
             * brightest a lit surface gets is the full albedo and the darkest
             * is ambient * albedo. With a dark ambient the terrain measured 21%
             * luminance on screen, which is not "moody" — it is unreadable.
             *
             * Ambient is high here because the sun term alone leaves any
             * surface facing away from the light at zero, and on a landscape
             * with slopes in every direction that is most of what is on
             * screen. A day/night cycle gets its range from the palette and the
             * sun direction, not from crushing the ambient. */
            float ambient = 0.55f + 0.30f * elevation;

            /* Fraction of the frame that reads as night, used to tint. */
            float night = 1.0f - elevation * 3.0f;
            if (night < 0.0f) night = 0.0f;
            if (night > 1.0f) night = 1.0f;

            /* Pull the albedos toward a cold blue-grey. Terrain green loses
             * the most saturation, which is what makes night read as blue
             * rather than merely dark. */
            /* Night tints toward a desaturated blue rather than dimming, so
             * night reads as cold rather than as a black screen. */
            static const float MOON[3] = {0.72f, 0.80f, 1.00f};

            if (g_litMaterial) {
                graphics_material_set_vec3(g_litMaterial, "cameraPos",
                                           g_camera.position[0],
                                           g_camera.position[1],
                                           g_camera.position[2]);
                graphics_material_set_vec3(g_litMaterial, "lightDir",
                                           -g_sunDirection[0],
                                           -g_sunDirection[1],
                                           -g_sunDirection[2]);
                graphics_material_set_float(g_litMaterial, "ambient", ambient);
                graphics_material_set_vec3(g_litMaterial, "albedo",
                                           0.85f + (MOON[0] - 0.85f) * night,
                                           0.55f + (MOON[1] - 0.55f) * night,
                                           0.25f + (MOON[2] - 0.25f) * night);
            }
            if (g_terrainMaterial) {
                graphics_material_set_vec3(g_terrainMaterial, "lightDir",
                                           -g_sunDirection[0],
                                           -g_sunDirection[1],
                                           -g_sunDirection[2]);
                graphics_material_set_float(g_terrainMaterial, "ambient", ambient);
                /* Night tints toward moonlight rather than toward a colour,
                 * since the texture already supplies the base colour. */
                graphics_material_set_vec3(g_terrainMaterial, "albedo",
                                           1.0f + (MOON[0] - 1.0f) * night,
                                           1.0f + (MOON[1] - 1.0f) * night,
                                           1.0f + (MOON[2] - 1.0f) * night);
            }
            if (g_gridMaterial) {
                graphics_material_set_vec3(g_gridMaterial, "lightDir",
                                           -g_sunDirection[0],
                                           -g_sunDirection[1],
                                           -g_sunDirection[2]);
                graphics_material_set_float(g_gridMaterial, "ambient", ambient);
                graphics_material_set_vec3(g_gridMaterial, "albedo",
                                           0.35f + (MOON[0] - 0.35f) * night,
                                           0.60f + (MOON[1] - 0.60f) * night,
                                           0.85f + (MOON[2] - 0.85f) * night);
            }
        }

        /* Skinning. model_pose_by_index() flattens the bone hierarchy into
         * skinning matrices for the given clip (or the rest pose when
         * clipIndex < 0). Static models have no bones and skip this; their
         * vertices all carry bone 0 at weight 1, which is the identity. */
        if (model_is_skinned(g_testModel)) {
            float t = (g_animInfo.name && g_animInfo.duration > 0.0f)
                    ? fmodf(g_elapsed, g_animInfo.duration)
                    : 0.0f;
            if (model_pose_by_index(g_testModel, g_animIndex, t, g_boneMatrices)) {
                graphics_setbones(g_testModel, g_boneMatrices,
                                  model_get_bone_count(g_testModel));
            }
        } else {
            /* Unskinned model: the shaders still read bone matrices, so bind
             * an identity set or buffer(3) is unbound and every vertex
             * collapses to the origin. */
            graphics_bindskinidentity();
        }

        /* Sky dome first. Its model matrix scales the unit dome out to
         * SKY_RADIUS and re-centres it on the camera, so it stays infinitely
         * far away while the character walks; the translation is written
         * directly because the camera-centred offset is not a world
         * position any scene node holds. Reverse-Z leaves the dome's depth at
         * the far plane, so everything drawn after overwrites it. */
        if (g_sky && g_skyMaterial) {
            float skyModel[16];
            math_identity(skyModel);
            math_scale(skyModel, skyModel, SKY_RADIUS, SKY_RADIUS, SKY_RADIUS);
            skyModel[12] = g_camera.position[0];
            skyModel[13] = g_camera.position[1];
            skyModel[14] = g_camera.position[2];

            graphics_material_set_mat4(g_skyMaterial, skyModel);
            graphics_material_set_vec3(g_skyMaterial, "cameraPos",
                                       g_camera.position[0],
                                       g_camera.position[1],
                                       g_camera.position[2]);
            graphics_bindtexture(g_skyGradient, 1);
            math_multiply(temp, view, skyModel);
            math_multiply(mvp, proj, temp);
            graphics_drawmodel(g_sky, g_skyMaterial, mvp);
        }


        scene_update(&g_scene);
        for (int i = 0; i < SCENE_MAX_NODES; i++) {
            const float *world = scene_world(&g_scene, i);
            if (!world) continue;

            /* mvp = proj * view * world */
            math_multiply(temp, view, world);
            math_multiply(mvp, proj, temp);

            /* World matrix drives world-space normals in the lit shader */
            if (g_litMaterial) {
                graphics_material_set_mat4(g_litMaterial, world);
            }
            graphics_bindmodeltexture(g_testModel, g_litMaterial);
            graphics_drawmodel(g_testModel, g_litMaterial, mvp);
        }

        /* Landscape. The mesh is already in world space, so its model matrix
         * is the identity and the MVP is just proj * view. It cannot reuse
         * `mvp` from the loop above, which still holds the last cube's
         * transform and would lift the whole landscape by that offset. */
        if (g_terrain && g_terrainMaterial) {
            static const float identity[16] = {
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1
            };
            graphics_material_set_vec3(g_terrainMaterial, "cameraPos",
                                       g_camera.position[0],
                                       g_camera.position[1],
                                       g_camera.position[2]);
            graphics_material_set_mat4(g_terrainMaterial, identity);
            if (g_groundTexture) {
                graphics_bindtexture(g_groundTexture, 1);
            } else {
                graphics_bindmodeltexture(g_terrain, g_terrainMaterial);
            }
            math_multiply(mvp, proj, view);
            graphics_drawmodel(g_terrain, g_terrainMaterial, mvp);
        }

        /* Instanced grid: 100 cubes placed from one per-instance transform
         * array. Metal draws them in a single call; the Vulkan backend loops
         * graphics_drawmodel() per instance for now (see its comment). */
        if (g_gridMaterial) {
            static float instanceTransforms[INSTANCED_GRID_COUNT][16];
            static float gridView[16];
            static float gridMvp[16];
            static float gridModel[16];
            static const float gridIdentity[16] = {
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1
            };
            int n = 0;

            math_identity(gridModel);
            math_scale(gridModel, gridModel, 0.12f, 0.12f, 0.12f);
            for (int gx = 0; gx < INSTANCED_GRID_SIZE; gx++) {
                for (int gz = 0; gz < INSTANCED_GRID_SIZE; gz++) {
                    /* Scattered across the landscape just in front of the
                     * cubes, each sitting on the terrain surface. */
                    float x = (gx - INSTANCED_GRID_SIZE / 2) * 0.6f;
                    float z = 8.0f + (gz - INSTANCED_GRID_SIZE / 2) * 0.6f;
                    float groundY = physics_terrain_height(x, z, TERRAIN_SEED,
                                                          TERRAIN_AMPLITUDE);
                    math_identity(instanceTransforms[n]);
                    math_translate(instanceTransforms[n], instanceTransforms[n],
                                   x, groundY + 0.06f, z);
                    math_multiply(instanceTransforms[n], gridModel, instanceTransforms[n]);
                    n++;
                }
            }

            graphics_material_set_vec3(g_gridMaterial, "cameraPos",
                                       g_camera.position[0],
                                       g_camera.position[1],
                                       g_camera.position[2]);

            /* Per-instance model matrices above; shared view-projection here */
            math_multiply(gridView, view, gridIdentity);
            math_multiply(gridMvp, proj, gridView);
            graphics_draw_instanced(g_testModel, g_gridMaterial,
                                    &instanceTransforms[0][0], gridMvp, (size_t)n);
        }
    }

    /* Draw FPS text overlay */
    if (!g_testText || !g_testFont) {
        return;
    }

    font_begin_batch(g_testFont);
    text_draw(g_testText, 8.0f, 8.0f, 0.0f,
              1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    /* Controls, so the bindings do not have to be remembered or guessed.
     * Skipped while the self test runs: it draws its results over this same
     * space, and drawing both at once overlaps them into an unreadable mess
     * of interleaved glyphs. */
    if (!g_selftestActive) {
        static const char *lines[] = {
            "WASD  move     Shift  sprint     Space  jump",
            "mouse  look     Esc  release cursor",
            ""
        };
        float y = 30.0f;
        for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
            text_set(g_helpText[i], lines[i]);
            text_draw(g_helpText[i], 8.0f, y, 0.0f, 1.0f, 1.0f,
                      0.0f, 0.0f, 0.0f, 0.0f);
            y += 18.0f;
        }

        /* Pointer state read back from the window rather than assumed from
         * the intent: a capture request can be refused by the platform, and
         * telling the player the cursor is trapped when it is not would make
         * Escape sound like the only way out. */
        {
            int captured = 0;
            window_getmousecapture(&captured);
            text_set(g_helpText[2], captured
                     ? "click or Esc to release the cursor"
                     : "the character walks the terrain; the world stays put");
            text_draw(g_helpText[2], 8.0f, y - 18.0f, 0.0f, 1.0f, 1.0f,
                      0.0f, 0.0f, 0.0f, 0.0f);
        }

    }

    /* Self test report, replacing the controls while it runs. The checks are
     * the reason to look at the screen during a run, so they take the space
     * rather than being appended under the help text. */
    if (g_selftestActive) {
        float ty = 30.0f;
        for (int i = 0; i < HELP_LINE_COUNT; i++) {
            text_set(g_helpText[i], i < HELP_LINE_COUNT - 1
                     ? selftest_line(&g_selftest, i)
                     : selftest_status(&g_selftest));
            text_draw(g_helpText[i], 8.0f, ty, 0.0f, 1.0f, 1.0f,
                      0.0f, 0.0f, 0.0f, 0.0f);
            ty += 18.0f;
        }
    }
    font_end_batch(g_testFont);
}

int framework_quit(void)
{
    audio_quit();
    return 1;
}

void framework_lowmemory(void)
{
}

void framework_visible(int visible)
{
    /* Deactivating the window does not always deliver a focus event, so
     * release input here too rather than only in framework_focus. */
    if (!visible) {
        framework_releaseinput();
        framework_set_look(0);
    }
}

void framework_move(int x, int y)
{
}

void framework_resize(int width, int height)
{
    (void)width; (void)height;
    graphics_resize();
}

void framework_minimize(void)
{
}

void framework_maximize(void)
{
}

void framework_restore(void)
{
}

void framework_mousefocus(int focus)
{
}

void framework_focus(int focus)
{
    /* Release every held key on focus loss. Key-up events are delivered to
     * the focused window, so without this the character keeps walking
     * forever after the window is switched away from. */
    if (!focus) {
        framework_releaseinput();
        framework_set_look(0);
    }
}

void framework_keypressed(const char *key, const char *scancode, int isrepeat)
{
    (void)key;
    if (isrepeat) return;

    /* Escape releases the pointer. Without this the cursor is trapped with no
     * way back to the rest of the desktop, which is the one thing pointer
     * capture must never do. */
    if (!strcmp(scancode, "Escape")) {
        framework_set_look(0);
        return;
    }

    if (!strcmp(scancode, "W")) g_keyForward = 1;
    else if (!strcmp(scancode, "S")) g_keyBack = 1;
    else if (!strcmp(scancode, "A")) g_keyLeft = 1;
    else if (!strcmp(scancode, "D")) g_keyRight = 1;
    /* Jump is edge-triggered, so holding Space does not bunny-hop. E and Q
     * used to fly the camera up and down; vertical motion now comes only
     * from gravity and the jump, so there is no binding that can lift the
     * character out of the world. */
    else if (!strcmp(scancode, "Space")) g_keyJump = 1;
    else if (!strcmp(scancode, "Left Shift") || !strcmp(scancode, "Right Shift")) g_keySprint = 1;
}

void framework_keyreleased(const char *key, const char *scancode)
{
    (void)key;

    if (!strcmp(scancode, "W")) g_keyForward = 0;
    else if (!strcmp(scancode, "S")) g_keyBack = 0;
    else if (!strcmp(scancode, "A")) g_keyLeft = 0;
    else if (!strcmp(scancode, "D")) g_keyRight = 0;
    else if (!strcmp(scancode, "Left Shift") || !strcmp(scancode, "Right Shift")) g_keySprint = 0;
}

void framework_textedited(const char *text, int start, int length)
{
}

void framework_textinput(const char *text)
{
}

/* Mouse look. Conventional first-person: a click captures the pointer and the
 * camera follows it continuously, Escape releases it. Holding a button to look
 * was the previous scheme and it is wrong twice over — the camera freezes
 * whenever the button is not down, so there is no way to look while walking,
 * and a right-button drag conflicts with anything else that wants that
 * button. */
void framework_mousemoved(int x, int y, int dx, int dy, int istouch)
{
    (void)x;
    (void)y;
    if (!g_lookActive || istouch) {
        return;
    }
    /* Ignore implausible jumps. A missed relative-mode release can deliver an
     * absolute-to-relative delta thousands of pixels wide, which whips the
     * camera around; clamping keeps that from looking like a spin. */
    const int kMaxDelta = 200;
    if (dx > kMaxDelta) dx = kMaxDelta;
    if (dx < -kMaxDelta) dx = -kMaxDelta;
    if (dy > kMaxDelta) dy = kMaxDelta;
    if (dy < -kMaxDelta) dy = -kMaxDelta;
    if (dx == 0 && dy == 0) {
        return;
    }
    camera_look(&g_camera, (float)dx, (float)dy, g_lookSensitivity);
}

/* Capture or release the pointer. Relative mode hides the cursor and reports
 * motion relative to the last position, which is what mouse look needs.
 *
 * Focus has to come first: an unfocused window cannot enter relative mode, so
 * asking for capture before the window is active is silently ignored and the
 * player gets a click that appears to do nothing. */
static void framework_set_look(int active)
{
    if (active == g_lookActive) {
        return;
    }
    if (active) {
        window_setkeyboardfocus(1);
        window_setmouserelative(1);
    } else {
        window_setmouserelative(0);
    }
    g_lookActive = active;
}

void framework_mousepressed(int x, int y, const char *button, int istouch)
{
    (void)x;
    (void)y;
    (void)button;
    if (istouch) {
        return;
    }
    /* Any click grabs the pointer, so the game is playable without aiming at
     * a particular button first. Clicking again while captured releases it,
     * which matters on a trackpad where Escape is awkward to reach. */
    framework_set_look(!g_lookActive);
}

void framework_mousereleased(int x, int y, const char *button, int istouch)
{
    /* Releasing no longer changes the look state: the camera tracks the
     * pointer as soon as it is captured, so there is no button to hold and no
     * release to miss. */
    (void)x;
    (void)y;
    (void)button;
    (void)istouch;
}

void framework_wheelmoved(int x, int y)
{
    /* The wheel does nothing. It used to zoom the camera, which read as an
     * engine that only responded to scrolling. */
    (void)x;
    (void)y;
}

void framework_joystick_axis_motion(int which, int axis, int value) { (void)which; (void)axis; (void)value; }
void framework_joystick_ball_motion(int which, int ball, int dx, int dy) { (void)which; (void)ball; (void)dx; (void)dy; }
void framework_joystick_hat_motion(int which, int hat, int value) { (void)which; (void)hat; (void)value; }
void framework_joystick_button_pressed(int which, int button) { (void)which; (void)button; }
void framework_joystick_button_released(int which, int button) { (void)which; (void)button; }
void framework_joystick_added(int which) { (void)which; }
void framework_joystick_removed(int which) { (void)which; }
void framework_gamepad_axis_motion(int which, int axis, int value) { (void)which; (void)axis; (void)value; }
void framework_gamepad_button_pressed(int which, int button) { (void)which; (void)button; }
void framework_gamepad_button_released(int which, int button) { (void)which; (void)button; }
void framework_gamepad_added(int which) { (void)which; }
void framework_gamepad_removed(int which) { (void)which; }
void framework_gamepad_remapped(int which) { (void)which; }
