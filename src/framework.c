/* Copyright Planimeter. All Rights Reserved. */

#include "filesystem.h"
#include "window.h"
#include "graphics.h"
#include "audio.h"
#include "font.h"
#include "text.h"
#include "math_c.h"
#include "camera.h"
#include "physics.h"
#include "player.h"
#include "scene.h"
#include "terrain.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

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

/* Procedural landscape. Generated at startup rather than loaded, so there
 * is no asset dependency and it is deterministic for a given seed.
 * Amplitude is modest relative to the extent so hills read as walkable
 * ground rather than cliffs that swallow the camera. */
#define TERRAIN_RESOLUTION 129
#define TERRAIN_EXTENT 600.0f
#define TERRAIN_AMPLITUDE 9.0f
#define TERRAIN_SEED 20261002u
static Model *g_terrain = NULL;
static Material g_terrainMaterial = NULL;

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

/* Controls overlay. Text objects are allocated once and rewritten each frame
 * by text_set(), which avoids per-frame allocation in the render loop. */
#define HELP_LINE_COUNT 3
static Text *g_helpText[HELP_LINE_COUNT];

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
    if (g_skyGradient) {
        graphics_destroytexture(g_skyGradient);
        g_skyGradient = NULL;
    }
}

/* Sky gradient as a 1x64 texture sampled by dome height. Row 0 is the
 * zenith, row 63 the horizon. Three-stop blend: deep blue overhead, pale
 * blue at eye level, and a warm haze right at the skyline so the terrain
 * silhouette has something to sit against. */
static void framework_create_sky_gradient(void)
{
    enum { SKY_GRADIENT_H = 64 };
    unsigned char pixels[SKY_GRADIENT_H * 4];

    static const struct { float t; float r, g, b; } stops[] = {
        {0.00f, 0.13f, 0.29f, 0.62f},  /* zenith */
        {0.55f, 0.36f, 0.60f, 0.86f},  /* upper sky */
        {0.88f, 0.68f, 0.80f, 0.92f},  /* pale horizon */
        {1.00f, 0.90f, 0.86f, 0.78f},  /* haze at the skyline */
    };
    const int stopCount = (int)(sizeof(stops) / sizeof(stops[0]));

    for (int y = 0; y < SKY_GRADIENT_H; y++) {
        float t = (float)y / (float)(SKY_GRADIENT_H - 1);

        int hi = 0;
        while (hi + 1 < stopCount && stops[hi + 1].t < t) {
            hi++;
        }
        int lo = hi > 0 ? hi - 1 : 0;
        float span = stops[hi].t - stops[lo].t;
        float f = span > 1e-6f ? (t - stops[lo].t) / span : 0.0f;

        pixels[y * 4 + 0] = (unsigned char)(255.0f * (stops[lo].r + (stops[hi].r - stops[lo].r) * f));
        pixels[y * 4 + 1] = (unsigned char)(255.0f * (stops[lo].g + (stops[hi].g - stops[lo].g) * f));
        pixels[y * 4 + 2] = (unsigned char)(255.0f * (stops[lo].b + (stops[hi].b - stops[lo].b) * f));
        pixels[y * 4 + 3] = 255;
    }

    g_skyGradient = graphics_createtexture_rgba(1, SKY_GRADIENT_H, pixels);
}

/* Build the dome and its material. Vertices are unit-sized so the model
 * matrix can carry both the radius and the camera-centred offset, and the
 * UV runs 0 at the zenith to 1 at the horizon to match the gradient rows.
 * Faces are wound so the inside is front-facing: the camera is inside. */
static void framework_create_sky(void)
{
    const int segments = SKY_SEGMENTS;
    const int rings = SKY_RINGS;
    const int vertexCount = (rings + 1) * (segments + 1);
    const int indexCount = rings * segments * 6;

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
        /* theta 0 at the zenith, pi/2 at the horizon. The dome stops at the
         * horizon: below that the terrain covers the view, and a full sphere
         * would show its underside through the gaps at the mesh edge. */
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

    /* Procedural landscape. */
    {
        void *mesh = terrain_build_mesh(TERRAIN_RESOLUTION, TERRAIN_EXTENT,
                                        TERRAIN_AMPLITUDE, TERRAIN_SEED);
        if (mesh) {
            uint32_t side = TERRAIN_RESOLUTION * TERRAIN_RESOLUTION;
            uint32_t quads = (TERRAIN_RESOLUTION - 1) * (TERRAIN_RESOLUTION - 1);
            /* terrain_build_mesh lays vertices then indices in one block. */
            g_terrain = graphics_createmesh(
                mesh, side, 22 * sizeof(float),
                (const uint32_t *)(mesh + (size_t)side * 22 * sizeof(float)),
                (size_t)quads * 6);
            terrain_free_mesh(mesh);
            if (g_terrain) {
                printf("Terrain: %dx%d grid, %.0f units across, seed %u\n",
                       TERRAIN_RESOLUTION, TERRAIN_RESOLUTION,
                       TERRAIN_EXTENT, TERRAIN_SEED);
            }
        }
        if (!g_terrain) {
            fprintf(stderr, "Failed to build terrain\n");
        }
    }

    framework_create_sky();

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
        graphics_material_set_vec3(g_litMaterial, "lightDir", 0.35f, -0.8f, -0.45f);
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
        graphics_material_set_vec3(g_gridMaterial, "lightDir", 0.35f, -0.8f, -0.45f);
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
        graphics_material_set_float(g_terrainMaterial, "diffuseSlot", -1.0f);
        graphics_material_set_float(g_terrainMaterial, "normalSlot", -1.0f);
        graphics_material_set_vec3(g_terrainMaterial, "lightDir", 0.35f, -0.8f, -0.45f);
        graphics_material_set_vec3(g_terrainMaterial, "cameraPos", 0.0f, 0.0f, 3.0f);
        graphics_material_set_vec3(g_terrainMaterial, "albedo", 0.32f, 0.46f, 0.24f);
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

        float baseY = terrain_height(0.0f, 0.0f, TERRAIN_SEED) * TERRAIN_AMPLITUDE;
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
        printf("POS %.3f %.3f %.3f GRND %d\n",
               g_player.position[0], g_player.position[1], g_player.position[2],
               g_player.grounded);
        fflush(stdout);
    }

    float dt = (float)deltaTime / 1000.0f;
    g_elapsed += dt;

    /* Scene cubes stay put. They used to orbit the root at
     * g_elapsed * 0.5, which was the only motion on screen before the
     * terrain existed and read as the whole world rotating. */

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

    /* Camera follows the character's eyes. Only orientation is its own, so
     * mouse-look turns the view without moving the player. */
    {
        float eye[3];
        player_eye(&g_player, eye);
        g_camera.position[0] = eye[0];
        g_camera.position[1] = eye[1];
        g_camera.position[2] = eye[2];
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

        /* Specular highlight tracks the camera */
        if (g_litMaterial) {
            graphics_material_set_vec3(g_litMaterial, "cameraPos",
                                       g_camera.position[0],
                                       g_camera.position[1],
                                       g_camera.position[2]);
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
            graphics_bindmodeltexture(g_terrain, g_terrainMaterial);
            math_multiply(mvp, proj, view);
            graphics_drawmodel(g_terrain, g_terrainMaterial, mvp);
        }

        /* Instanced grid: 100 cubes placed from one per-instance transform
         * array. Metal draws them in a single call; the Vulkan backend loops
         * graphics_drawmodel() per instance for now (see its comment).
         * Set NO_INSTANCED_GRID=1 to skip the whole pass, which isolates
         * instanced-draw crashes from the rest of the frame. */
        if (g_gridMaterial && !getenv("NO_INSTANCED_GRID")) {
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
                    float groundY = (g_terrain
                        ? terrain_height(x, z, TERRAIN_SEED) * TERRAIN_AMPLITUDE
                        : 0.0f);
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
    /* Controls, so the bindings do not have to be remembered or guessed. */
    {
        static const char *lines[] = {
            "WASD  move     Shift  sprint     Space  jump",
            "right-drag  look",
            "the character walks the terrain; the world stays put"
        };
        float y = 30.0f;
        for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
            text_set(g_helpText[i], lines[i]);
            text_draw(g_helpText[i], 8.0f, y, 0.0f, 1.0f, 1.0f,
                      0.0f, 0.0f, 0.0f, 0.0f);
            y += 18.0f;
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
        window_setmouserelative(0);
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
        window_setmouserelative(0);
    }
}

void framework_keypressed(const char *key, const char *scancode, int isrepeat)
{
    (void)key;
    if (isrepeat) return;

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

void framework_mousemoved(int x, int y, int dx, int dy, int istouch)
{
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

void framework_mousepressed(int x, int y, const char *button, int istouch)
{
    if (!istouch && button && !strcmp(button, "right")) {
        g_lookActive = 1;
        window_setmouserelative(1);
    } else if (!istouch && button && !strcmp(button, "left")) {
        /* Left click grabs keyboard focus. SDL delivers key events to the
         * focused window, so without this the controls appear dead until
         * the user clicks. */
        window_setkeyboardfocus(1);
    }
}

void framework_mousereleased(int x, int y, const char *button, int istouch)
{
    if (!istouch && button && !strcmp(button, "right")) {
        /* Always drop relative mode on release. If a release is ever missed
         * the camera would otherwise keep spinning from stray deltas with no
         * way back. */
        g_lookActive = 0;
        window_setmouserelative(0);
    }
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
