/* Copyright Planimeter. All Rights Reserved. */

#include "postprocess.h"
#include "graphics.h"
#include "window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The compositing shaders are embedded MSL, the way the backend's default and
 * text shaders are, because Metal compiles vertex_main and fragment_main out of
 * one source. A backend that compiles GLSL would need the two stages as
 * separate sources, which belongs with the offscreen-target support it does not
 * have yet. */
#if defined(__APPLE__)
#define POSTPROCESS_SUPPORTED 1
#else
#define POSTPROCESS_SUPPORTED 0
#endif

static Texture    g_sceneColor = NULL;
static RenderPass g_scenePass  = NULL;
static Shader     g_postVert   = NULL;
static Shader     g_postFrag   = NULL;
static int        g_width      = 0;
static int        g_height     = 0;
static int        g_ready      = 0;
static int        g_active     = 0;  /* the scene pass is open */
static ColorFormat g_sceneFormat = COLOR_FORMAT_RGBA16F;

/* Scene target format. RGBA16F is the shipping choice: float storage, so
 * effects see values above 1.0 instead of a clipped 8-bit frame.
 *
 * It is not bit-exact against rendering straight to the swapchain. A half
 * holds 11 bits of significand, an 8-bit swapchain step is 1/255, and the
 * two do not divide, so a composited frame differs from a direct one by one
 * level wherever the shader's value lands within a half-quantum of an 8-bit
 * rounding boundary — 6% of the terrain below, measured. GAME_POSTPROCESS_FORMAT=32f
 * swaps in a 32-bit float target, which stores shader output without rounding
 * at all; a capture then matches a direct render byte for byte, which is how
 * the composite is shown to be exact and the 16-bit deviation shown to be
 * storage precision and nothing else. */
static void postprocess_pick_format(void)
{
    const char *env = getenv("GAME_POSTPROCESS_FORMAT");
    if (env && strncmp(env, "32", 2) == 0) {
        g_sceneFormat = COLOR_FORMAT_RGBA32F;
    }
}

static const char *postprocess_format_name(void)
{
    return g_sceneFormat == COLOR_FORMAT_RGBA32F ? "RGBA32F" : "RGBA16F";
}

#if POSTPROCESS_SUPPORTED
static const char *POST_MSL =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "\n"
    "struct VertexIn {\n"
    "    float3 position [[attribute(0)]];\n"
    "    float2 uv       [[attribute(1)]];\n"
    "};\n"
    "\n"
    "struct VertexOut {\n"
    "    float4 position [[position]];\n"
    "    float2 uv;\n"
    "};\n"
    "\n"
    /* The fullscreen triangle is already in clip space, so this only passes\n
     * it through; uv follows the same mapping the geometry was built with. */
    "vertex VertexOut vertex_main(VertexIn v [[stage_in]]) {\n"
    "    VertexOut out;\n"
    "    out.position = float4(v.position.xy, 0.0, 1.0);\n"
    "    out.uv = v.uv;\n"
    "    return out;\n"
    "}\n"
    "\n"
    /* No effect enabled: the scene is read back exactly as it was written, so
     * the composited frame is the direct render. Alpha is forced opaque since
     * the scene target carries no meaningful alpha. */
    "fragment float4 fragment_main(VertexOut in [[stage_in]],\n"
    "                              texture2d<float> sceneTex [[texture(1)]],\n"
    "                              sampler sceneSampler [[sampler(1)]]) {\n"
    "    float4 scene = sceneTex.sample(sceneSampler, in.uv);\n"
    "    return float4(scene.rgb, 1.0);\n"
    "}\n";
#endif

/* Recreate the scene target at the current drawable size. Returns 0 when the
 * backend cannot provide one, which leaves the chain disabled. */
static int postprocess_ensure_target(void)
{
#if POSTPROCESS_SUPPORTED
    int w = 0, h = 0;
    window_getwindowsizeinpixels(&w, &h);
    if (w < 1 || h < 1) return 0;

    if (g_sceneColor && w == g_width && h == g_height) return 1;

    Texture target = graphics_createtexture_target(w, h, g_sceneFormat);
    if (!target) return 0;

    /* Attach the new target before dropping the old one: the pass holds the
     * pointer it was given, so destroying first would leave it dangling. */
    graphics_pass_set_color_texture(g_scenePass, target, 0);
    if (g_sceneColor) graphics_destroytexture(g_sceneColor);

    g_sceneColor = target;
    g_width = w;
    g_height = h;
    return 1;
#else
    return 0;
#endif
}

void postprocess_init(void)
{
#if POSTPROCESS_SUPPORTED
    postprocess_pick_format();

    /* Depth testing on: the scene pass carries a depth attachment, which the
     * backend creates to match the target. */
    RasterState state = {0};
    state.depthWrite = 1;
    state.depthTest = 1;

    g_scenePass = graphics_createpass("scene", state);
    if (!g_scenePass) {
        fprintf(stderr, "Post-process: could not create the scene pass; "
                        "drawing directly to the swapchain\n");
        return;
    }

    g_postVert = graphics_createshader(SHADER_STAGE_VERTEX,
                                       POST_MSL, strlen(POST_MSL), NULL, 0);
    g_postFrag = graphics_createshader(SHADER_STAGE_FRAGMENT,
                                       POST_MSL, strlen(POST_MSL), NULL, 0);
    if (!g_postVert || !g_postFrag) {
        fprintf(stderr, "Post-process: compositing shaders failed to compile; "
                        "drawing directly to the swapchain\n");
        postprocess_shutdown();
        return;
    }

    if (!postprocess_ensure_target()) {
        fprintf(stderr, "Post-process: no offscreen target on this backend; "
                        "drawing directly to the swapchain\n");
        postprocess_shutdown();
        return;
    }

    g_ready = 1;
    printf("Post-process: %s scene target %dx%d\n",
           postprocess_format_name(), g_width, g_height);
#endif
}

void postprocess_shutdown(void)
{
    g_active = 0;
    g_ready = 0;

    if (g_scenePass) {
        graphics_destroypass(g_scenePass);
        g_scenePass = NULL;
    }
    if (g_sceneColor) {
        graphics_destroytexture(g_sceneColor);
        g_sceneColor = NULL;
    }
    if (g_postVert) {
        graphics_destroyshader(g_postVert);
        g_postVert = NULL;
    }
    if (g_postFrag) {
        graphics_destroyshader(g_postFrag);
        g_postFrag = NULL;
    }
    g_width = 0;
    g_height = 0;
}

void postprocess_begin(void)
{
    if (!g_ready || g_active) return;

    /* Nothing can be recorded without a drawable, and the target would be
     * sized to a zero-width window. */
    if (graphics_isminimized()) return;
    if (!postprocess_ensure_target()) return;

    graphics_beginpass(g_scenePass);
    g_active = 1;
}

void postprocess_end(void)
{
    if (!g_active) return;
    g_active = 0;

    graphics_endpass(g_scenePass);

    /* Resume the swapchain and composite over the whole frame. Leaving that
     * pass open is deliberate: the 2D overlay draws into it next, outside the
     * effect, so text stays sharp and undistorted. */
    graphics_beginpass(NULL);
    graphics_draw_fullscreen(g_sceneColor, g_postVert, g_postFrag, NULL);
}
