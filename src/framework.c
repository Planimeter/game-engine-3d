/* Copyright Planimeter. All Rights Reserved. */

#include "filesystem.h"
#include "window.h"
#include "graphics.h"
#include "audio.h"
#include "font.h"
#include "text.h"
#include "math_c.h"
#include "camera.h"
#include "scene.h"
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

/* Camera + scene graph */
static Camera g_camera;
static Scene g_scene;
static int g_rootNode = -1;

/* Camera control input state (see framework_keypressed/keyreleased) */
static int g_keyForward = 0;
static int g_keyBack = 0;
static int g_keyLeft = 0;
static int g_keyRight = 0;
static int g_keyUp = 0;
static int g_keyDown = 0;
static int g_lookActive = 0;
static const float g_moveSpeed = 3.0f;      /* world units per second */
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
                snprintf(modelPath, sizeof(modelPath), "%s/Models/cube.obj", resolved);
                g_testModel = graphics_loadmodel(modelPath);
                if (g_testModel) {
                    printf("Loaded cube model from %s (%d meshes)\n",
                           modelPath, model_get_mesh_count(g_testModel));
                } else {
                    fprintf(stderr, "Failed to load cube model from %s\n", modelPath);
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

    /* Lit material: floats are ambient/specular/shininess, vec3s are
     * light direction/camera position/albedo (slot order matters — the
     * material API packs by slot, not by name). The model matrix is
     * updated per frame via graphics_material_set_mat4. */
    g_litMaterial = graphics_creatematerial(NULL);
    if (g_litMaterial) {
        graphics_material_set_float(g_litMaterial, "ambient", 0.15f);
        graphics_material_set_float(g_litMaterial, "specular", 0.45f);
        graphics_material_set_float(g_litMaterial, "shininess", 32.0f);
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

    /* Fly camera: WASD + E/Q to move, right-drag to look, wheel to zoom */
    camera_init(&g_camera, 0.0f, 0.6f, 4.0f);

    /* Scene graph: spinning root with three children, so parent->child
     * transform composition is visible (children orbit the root). */
    scene_init(&g_scene);
    g_rootNode = scene_create(&g_scene, -1);
    if (g_rootNode >= 0) {
        int childLeft = scene_create(&g_scene, g_rootNode);
        int childRight = scene_create(&g_scene, g_rootNode);
        int childTop = scene_create(&g_scene, g_rootNode);

        scene_setposition(&g_scene, childLeft, -1.3f, 0.0f, 0.0f);
        scene_setscale(&g_scene, childLeft, 0.5f, 0.5f, 0.5f);
        scene_setposition(&g_scene, childRight, 1.3f, 0.0f, 0.0f);
        scene_setscale(&g_scene, childRight, 0.5f, 0.5f, 0.5f);
        scene_setposition(&g_scene, childTop, 0.0f, 1.3f, 0.0f);
        scene_setscale(&g_scene, childTop, 0.5f, 0.5f, 0.5f);
    }
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

    /* Spin the scene root; the children orbit with it */
    if (g_rootNode >= 0) {
        scene_setrotation(&g_scene, g_rootNode, 0.0f, g_elapsed * 0.5f, 0.0f);
    }

    /* Fly movement along the camera basis */
    float fwd = (g_keyForward ? 1.0f : 0.0f) - (g_keyBack ? 1.0f : 0.0f);
    float rgt = (g_keyRight ? 1.0f : 0.0f) - (g_keyLeft ? 1.0f : 0.0f);
    float up = (g_keyUp ? 1.0f : 0.0f) - (g_keyDown ? 1.0f : 0.0f);
    if (fwd != 0.0f || rgt != 0.0f || up != 0.0f) {
        camera_move(&g_camera, fwd, rgt, up, g_moveSpeed * dt);
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

        /* Camera-driven projection and view */
        camera_projection(&g_camera, aspect, proj);
        camera_view(&g_camera, view);

        /* Specular highlight tracks the camera */
        if (g_litMaterial) {
            graphics_material_set_vec3(g_litMaterial, "cameraPos",
                                       g_camera.position[0],
                                       g_camera.position[1],
                                       g_camera.position[2]);
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
            graphics_drawmodel(g_testModel, g_litMaterial, mvp);
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
                    /* Camera sits at +z looking down -Z, so the grid goes at
                     * negative z (in front of the origin) and below the
                     * scene graph cubes to stay clear of them */
                    float x = (gx - INSTANCED_GRID_SIZE / 2) * 0.2f;
                    float z = -2.2f + (gz - INSTANCED_GRID_SIZE / 2) * 0.2f;
                    math_identity(instanceTransforms[n]);
                    math_translate(instanceTransforms[n], instanceTransforms[n], x, -0.9f, z);
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
}

void framework_keypressed(const char *key, const char *scancode, int isrepeat)
{
    (void)key;
    if (isrepeat) return;

    if (!strcmp(scancode, "W")) g_keyForward = 1;
    else if (!strcmp(scancode, "S")) g_keyBack = 1;
    else if (!strcmp(scancode, "A")) g_keyLeft = 1;
    else if (!strcmp(scancode, "D")) g_keyRight = 1;
    else if (!strcmp(scancode, "E")) g_keyUp = 1;
    else if (!strcmp(scancode, "Q")) g_keyDown = 1;
}

void framework_keyreleased(const char *key, const char *scancode)
{
    (void)key;

    if (!strcmp(scancode, "W")) g_keyForward = 0;
    else if (!strcmp(scancode, "S")) g_keyBack = 0;
    else if (!strcmp(scancode, "A")) g_keyLeft = 0;
    else if (!strcmp(scancode, "D")) g_keyRight = 0;
    else if (!strcmp(scancode, "E")) g_keyUp = 0;
    else if (!strcmp(scancode, "Q")) g_keyDown = 0;
}

void framework_textedited(const char *text, int start, int length)
{
}

void framework_textinput(const char *text)
{
}

void framework_mousemoved(int x, int y, int dx, int dy, int istouch)
{
    if (g_lookActive && !istouch) {
        camera_look(&g_camera, (float)dx, (float)dy, g_lookSensitivity);
    }
}

void framework_mousepressed(int x, int y, const char *button, int istouch)
{
    if (!istouch && button && !strcmp(button, "right")) {
        g_lookActive = 1;
        window_setmouserelative(1);
    }
}

void framework_mousereleased(int x, int y, const char *button, int istouch)
{
    if (!istouch && button && !strcmp(button, "right")) {
        g_lookActive = 0;
        window_setmouserelative(0);
    }
}

void framework_wheelmoved(int x, int y)
{
    (void)x;
    camera_zoom(&g_camera, (float)y);
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
