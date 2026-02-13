/* Copyright Planimeter. All Rights Reserved. */

#include "font.h"
#include "filesystem.h"
#include "graphics.h"
#include "window.h"
#include "job.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern JobSystem *g_jobSystem;

#define FONT_ATLAS_SIZE 1024
#define FONT_GLYPH_PADDING 1
#define INITIAL_BATCH_CAPACITY 64
#define MAX_LINES_PER_TEXT 256

typedef struct {
    uint32_t glyph_id;
    int x;
    int y;
    int width;
    int height;
    int bearing_x;
    int bearing_y;
    float advance_x;
    float u0;
    float v0;
    float u1;
    float v1;
} Glyph;

typedef struct {
    float position[3];
    float texCoords[2];
} TextVertex;

typedef struct {
    const char *text;
    size_t text_len;
    hb_glyph_info_t *infos;
    hb_glyph_position_t *positions;
    unsigned int glyph_count;
    float origin_x;
    float origin_y;
    float sx;
    float sy;
} ShapedLine;

typedef struct {
    char *text;
    float x;
    float y;
    float sx;
    float sy;
} TextDrawCommand;

typedef struct {
    TextDrawCommand *commands;
    size_t count;
    size_t capacity;
} TextBatch;

struct Font {
    FT_Face face;
    hb_font_t *hb_font;
    hb_buffer_t *hb_buffer;
    unsigned char *file_data;
    size_t file_size;
    int size_px;
    int ascent;
    int descent;
    int height;
    int atlas_width;
    int atlas_height;
    int pen_x;
    int pen_y;
    int row_height;
    Texture atlas;
    Glyph *glyphs;
    size_t glyph_count;
    size_t glyph_capacity;
    Buffer vertex_buffer;
    Buffer index_buffer;
    size_t vertex_capacity;
    size_t index_capacity;
    Pipeline pipeline;
    
    /* Batching system */
    TextBatch batch;
    TextVertex *batch_vertices;
    uint32_t *batch_indices;
    size_t batch_vertex_capacity;
    size_t batch_index_capacity;
    
    /* Thread-local HarfBuzz buffers */
    hb_buffer_t **worker_hb_buffers;
    size_t worker_buffer_count;
};

static FT_Library g_ft_library;
static int g_ft_ready;

static unsigned char *g_text_vert_spv;
static size_t g_text_vert_spv_size;
static unsigned char *g_text_frag_spv;
static size_t g_text_frag_spv_size;
static int g_text_spv_ready;

static const char *g_text_vert_source =
    "#version 450\n"
    "layout(location = 0) in vec3 in_position;\n"
    "layout(location = 1) in vec2 in_texcoord;\n"
    "layout(location = 0) out vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(in_position, 1.0);\n"
    "    v_texcoord = in_texcoord;\n"
    "}\n";

static const char *g_text_frag_source =
    "#version 450\n"
    "layout(set = 0, binding = 0) uniform sampler2D tex[];\n"
    "layout(location = 0) in vec2 v_texcoord;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "void main() {\n"
    "    vec4 sample = texture(tex[0], v_texcoord);\n"
    "    out_color = vec4(1.0, 1.0, 1.0, sample.r);\n"
    "}\n";

static int font_init_freetype()
{
    if (g_ft_ready) {
        return 1;
    }

    if (FT_Init_FreeType(&g_ft_library) != 0) {
        return 0;
    }

    g_ft_ready = 1;
    return 1;
}

static int font_load_text_shaders()
{
    if (g_text_spv_ready) {
        return 1;
    }

    g_text_vert_spv_size = filesystem_fileread((void **)&g_text_vert_spv, "shaders/text.vert.spv");
    g_text_frag_spv_size = filesystem_fileread((void **)&g_text_frag_spv, "shaders/text.frag.spv");

    if (g_text_vert_spv_size == 0 || g_text_frag_spv_size == 0) {
        fprintf(stderr, "Failed to load text shaders\n");
        return 0;
    }

    g_text_spv_ready = 1;
    return 1;
}

static Glyph *font_find_glyph(Font *font, uint32_t glyph_id)
{
    for (size_t i = 0; i < font->glyph_count; i++) {
        if (font->glyphs[i].glyph_id == glyph_id) {
            return &font->glyphs[i];
        }
    }
    return NULL;
}

static Glyph *font_add_glyph(Font *font, uint32_t glyph_id)
{
    if (font->glyph_count == font->glyph_capacity) {
        size_t new_capacity = font->glyph_capacity == 0 ? 64 : font->glyph_capacity * 2;
        Glyph *new_glyphs = (Glyph *)realloc(font->glyphs, new_capacity * sizeof(Glyph));
        if (!new_glyphs) {
            return NULL;
        }
        font->glyphs = new_glyphs;
        font->glyph_capacity = new_capacity;
    }

    font->glyphs[font->glyph_count].glyph_id = glyph_id;
    return &font->glyphs[font->glyph_count++];
}

static int font_pack_glyph(Font *font, int width, int height, int *out_x, int *out_y)
{
    if (width == 0 || height == 0) {
        *out_x = font->pen_x;
        *out_y = font->pen_y;
        return 1;
    }

    if (font->pen_x + width + FONT_GLYPH_PADDING > font->atlas_width) {
        font->pen_x = FONT_GLYPH_PADDING;
        font->pen_y += font->row_height + FONT_GLYPH_PADDING;
        font->row_height = 0;
    }

    if (font->pen_y + height + FONT_GLYPH_PADDING > font->atlas_height) {
        return 0;
    }

    *out_x = font->pen_x;
    *out_y = font->pen_y;

    font->pen_x += width + FONT_GLYPH_PADDING;
    if (height > font->row_height) {
        font->row_height = height;
    }

    return 1;
}

static Glyph *font_load_glyph(Font *font, uint32_t glyph_id)
{
    FT_GlyphSlot slot = font->face->glyph;
    Glyph *glyph = font_add_glyph(font, glyph_id);

    if (!glyph) {
        return NULL;
    }

    if (FT_Load_Glyph(font->face, glyph_id, FT_LOAD_DEFAULT) != 0) {
        return NULL;
    }

    if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
        return NULL;
    }

    glyph->width = (int)slot->bitmap.width;
    glyph->height = (int)slot->bitmap.rows;
    glyph->bearing_x = slot->bitmap_left;
    glyph->bearing_y = slot->bitmap_top;
    glyph->advance_x = slot->advance.x / 64.0f;

    if (!font_pack_glyph(font, glyph->width, glyph->height, &glyph->x, &glyph->y)) {
        return NULL;
    }

    if (glyph->width > 0 && glyph->height > 0) {
        size_t pixel_count = (size_t)glyph->width * (size_t)glyph->height;
        size_t rgba_size = pixel_count * 4;
        unsigned char *rgba = (unsigned char *)malloc(rgba_size);

        if (!rgba) {
            return NULL;
        }

        for (size_t i = 0; i < pixel_count; i++) {
            unsigned char alpha = slot->bitmap.buffer[i];
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = alpha;
        }

        graphics_updatetexture(font->atlas, glyph->x, glyph->y, glyph->width, glyph->height, rgba);
        free(rgba);
    }

    glyph->u0 = (float)glyph->x / (float)font->atlas_width;
    glyph->v0 = (float)glyph->y / (float)font->atlas_height;
    glyph->u1 = (float)(glyph->x + glyph->width) / (float)font->atlas_width;
    glyph->v1 = (float)(glyph->y + glyph->height) / (float)font->atlas_height;

    return glyph;
}

static Glyph *font_get_glyph(Font *font, uint32_t glyph_id)
{
    Glyph *glyph = font_find_glyph(font, glyph_id);
    if (glyph) {
        return glyph;
    }

    return font_load_glyph(font, glyph_id);
}

static void font_pixel_to_ndc(float px, float py, int w, int h, float *out_x, float *out_y)
{
    *out_x = (px / (float)w) * 2.0f - 1.0f;
    *out_y = -1.0f + (py / (float)h) * 2.0f;
}

static void font_build_vertices(Font *font,
                                const hb_glyph_info_t *infos,
                                const hb_glyph_position_t *positions,
                                size_t count,
                                float origin_x,
                                float origin_y,
                                float sx,
                                float sy,
                                TextVertex *vertices,
                                uint32_t *indices,
                                size_t *out_index_count)
{
    int win_w = 0;
    int win_h = 0;
    float pen_x = origin_x;
    float baseline = origin_y + font->ascent * sy;

    window_getwindowsizeinpixels(&win_w, &win_h);

    for (size_t i = 0; i < count; i++) {
        uint32_t glyph_id = infos[i].codepoint;
        Glyph *glyph = font_get_glyph(font, glyph_id);
        float x_offset = positions[i].x_offset / 64.0f;
        float y_offset = positions[i].y_offset / 64.0f;
        float x_advance = positions[i].x_advance / 64.0f;
        float y_advance = positions[i].y_advance / 64.0f;
        size_t vbase = i * 4;
        size_t ibase = i * 6;
        float gx;
        float gy;
        float w;
        float h;
        float x0;
        float y0;
        float x1;
        float y1;

        if (!glyph) {
            pen_x += x_advance * sx;
            baseline += y_advance * sy;
            continue;
        }

        gx = pen_x + (x_offset + glyph->bearing_x) * sx;
        gy = baseline - glyph->bearing_y * sy - y_offset * sy;
        w = glyph->width * sx;
        h = glyph->height * sy;

        for (size_t j = 0; j < 4; j++) {
            vertices[vbase + j].position[0] = 0.0f;
            vertices[vbase + j].position[1] = 0.0f;
            vertices[vbase + j].position[2] = 0.0f;
            vertices[vbase + j].texCoords[0] = 0.0f;
            vertices[vbase + j].texCoords[1] = 0.0f;
        }

        if (w > 0.0f && h > 0.0f) {
            font_pixel_to_ndc(gx, gy, win_w, win_h, &x0, &y0);
            font_pixel_to_ndc(gx + w, gy, win_w, win_h, &x1, &y0);
            font_pixel_to_ndc(gx, gy + h, win_w, win_h, &x0, &y1);
            font_pixel_to_ndc(gx + w, gy + h, win_w, win_h, &x1, &y1);

            vertices[vbase + 0].position[0] = x0;
            vertices[vbase + 0].position[1] = y0;
            vertices[vbase + 0].position[2] = 0.0f;
            vertices[vbase + 1].position[0] = x1;
            vertices[vbase + 1].position[1] = y0;
            vertices[vbase + 1].position[2] = 0.0f;
            vertices[vbase + 2].position[0] = x0;
            vertices[vbase + 2].position[1] = y1;
            vertices[vbase + 2].position[2] = 0.0f;
            vertices[vbase + 3].position[0] = x1;
            vertices[vbase + 3].position[1] = y1;
            vertices[vbase + 3].position[2] = 0.0f;

            vertices[vbase + 0].texCoords[0] = glyph->u0;
            vertices[vbase + 0].texCoords[1] = glyph->v0;
            vertices[vbase + 1].texCoords[0] = glyph->u1;
            vertices[vbase + 1].texCoords[1] = glyph->v0;
            vertices[vbase + 2].texCoords[0] = glyph->u0;
            vertices[vbase + 2].texCoords[1] = glyph->v1;
            vertices[vbase + 3].texCoords[0] = glyph->u1;
            vertices[vbase + 3].texCoords[1] = glyph->v1;
        }

        indices[ibase + 0] = (uint32_t)(vbase + 0);
        indices[ibase + 1] = (uint32_t)(vbase + 1);
        indices[ibase + 2] = (uint32_t)(vbase + 2);
        indices[ibase + 3] = (uint32_t)(vbase + 2);
        indices[ibase + 4] = (uint32_t)(vbase + 1);
        indices[ibase + 5] = (uint32_t)(vbase + 3);

        pen_x += x_advance * sx;
        baseline += y_advance * sy;
    }

    *out_index_count = count * 6;
}

int font_get_ascent(Font *font) {
    if (!font) return 0;
    return font->ascent;
}

Font *font_create(const char *filepath, int size)
{
    Font *font;
    void *file_data = NULL;
    size_t file_size;
    int pixel_size;
    unsigned char *atlas_pixels;

    if (!filepath || size <= 0) {
        fprintf(stderr, "font_create: invalid filepath or size\n");
        return NULL;
    }
    if (!font_init_freetype()) {
        fprintf(stderr, "font_create: failed to initialize FreeType\n");
        return NULL;
    }
    if (!font_load_text_shaders()) {
        fprintf(stderr, "font_create: failed to load text shaders\n");
        return NULL;
    }

    font = (Font *)calloc(1, sizeof(Font));
    if (!font) {
        return NULL;
    }

    file_size = filesystem_fileread(&file_data, filepath);
    if (file_size == 0 || !file_data) {
        fprintf(stderr, "font_create: failed to read font file '%s'\n", filepath);
        free(font);
        return NULL;
    }

    font->file_data = (unsigned char *)file_data;
    font->file_size = file_size;
    pixel_size = size;

    if (FT_New_Memory_Face(g_ft_library, font->file_data, (FT_Long)font->file_size, 0, &font->face) != 0) {
        fprintf(stderr, "font_create: FreeType failed to load font '%s'\n", filepath);
        free(font->file_data);
        free(font);
        return NULL;
    }

    FT_Set_Pixel_Sizes(font->face, 0, pixel_size);

    font->size_px = size;
    font->ascent = (int)(font->face->size->metrics.ascender / 64);
    font->descent = (int)(font->face->size->metrics.descender / 64);
    font->height = (int)(font->face->size->metrics.height / 64);

    font->hb_font = hb_ft_font_create_referenced(font->face);
    font->hb_buffer = hb_buffer_create();

    font->atlas_width = FONT_ATLAS_SIZE;
    font->atlas_height = FONT_ATLAS_SIZE;
    font->pen_x = FONT_GLYPH_PADDING;
    font->pen_y = FONT_GLYPH_PADDING;
    font->row_height = 0;

    atlas_pixels = (unsigned char *)calloc(1, (size_t)font->atlas_width * (size_t)font->atlas_height * 4);
    if (!atlas_pixels) {
        font_destroy(font);
        return NULL;
    }

    font->atlas = graphics_createtexture_rgba(font->atlas_width, font->atlas_height, atlas_pixels);
    free(atlas_pixels);

    if (!font->atlas) {
        font_destroy(font);
        return NULL;
    }

    if (g_text_spv_ready) {
        Shader vert = graphics_createshader((const char *)g_text_vert_spv, g_text_vert_spv_size, NULL, 0);
        Shader frag = graphics_createshader((const char *)g_text_frag_spv, g_text_frag_spv_size, NULL, 0);
        
        if (vert && frag) {
            RasterState state = {0};
            state.depthWrite = 0;
            state.depthTest = 0;
            state.backfaceCulling = 0;
            state.blendMode = BLEND_ALPHA;
            
            font->pipeline = graphics_createpipeline(vert, frag, VERTEX_FORMAT_POS_UV, state);
        }
        
        graphics_destroyshader(vert);
        graphics_destroyshader(frag);
    }

    return font;
}

void font_destroy(Font *font)
{
    if (!font) {
        return;
    }

    if (font->pipeline) {
        graphics_destroypipeline(font->pipeline);
    }
    if (font->vertex_buffer) {
        graphics_destroybuffer(font->vertex_buffer);
    }
    if (font->index_buffer) {
        graphics_destroybuffer(font->index_buffer);
    }
    if (font->atlas) {
        graphics_destroytexture(font->atlas);
    }

    if (font->hb_buffer) {
        hb_buffer_destroy(font->hb_buffer);
    }
    if (font->hb_font) {
        hb_font_destroy(font->hb_font);
    }

    if (font->face) {
        FT_Done_Face(font->face);
    }

    for (size_t i = 0; i < font->batch.count; i++) {
        free(font->batch.commands[i].text);
    }
    free(font->batch.commands);
    free(font->batch_vertices);
    free(font->batch_indices);
    
    if (font->worker_hb_buffers) {
        for (size_t i = 0; i < font->worker_buffer_count; i++) {
            if (font->worker_hb_buffers[i]) {
                hb_buffer_destroy(font->worker_hb_buffers[i]);
            }
        }
        free(font->worker_hb_buffers);
    }

    free(font->glyphs);
    free(font->file_data);
    free(font);
}

int font_get_width(Font *font, const char *text)
{
    size_t max_width = 0;
    float line_width = 0.0f;
    const char *line_start;
    const char *line_end;

    if (!font || !text) {
        return 0;
    }

    line_start = text;
    while (*line_start) {
        line_end = strchr(line_start, '\n');
        if (!line_end) {
            line_end = line_start + strlen(line_start);
        }

        hb_buffer_clear_contents(font->hb_buffer);
        hb_buffer_add_utf8(font->hb_buffer, line_start, (int)(line_end - line_start), 0, (int)(line_end - line_start));
        hb_buffer_guess_segment_properties(font->hb_buffer);
        hb_shape(font->hb_font, font->hb_buffer, NULL, 0);

        line_width = 0.0f;
        unsigned int count = 0;
        hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(font->hb_buffer, &count);
        for (unsigned int i = 0; i < count; i++) {
            line_width += positions[i].x_advance / 64.0f;
        }

        if ((size_t)line_width > max_width) {
            max_width = (size_t)line_width;
        }

        if (*line_end == '\n') {
            line_start = line_end + 1;
        } else {
            break;
        }
    }

    return (int)(max_width + 0.5f);
}

int font_get_height(Font *font)
{
    if (!font) {
        return 0;
    }

    return font->height;
}

void font_print(Font *font,
                const char *text,
                float x, float y,
                float r,
                float sx, float sy,
                float ox, float oy,
                float kx, float ky)
{
    const char *line_start;
    const char *line_end;
    float origin_x = x;
    float cursor_y = y;

    (void)r;
    (void)ox;
    (void)oy;
    (void)kx;
    (void)ky;

    if (!font || !text) {
        return;
    }

    if (sx == 0.0f) {
        sx = 1.0f;
    }
    if (sy == 0.0f) {
        sy = 1.0f;
    }

    line_start = text;
    while (*line_start) {
        size_t line_len;
        unsigned int glyph_count;
        hb_glyph_info_t *infos;
        hb_glyph_position_t *positions;
        size_t vertex_count;
        size_t index_count;
        TextVertex *vertices;
        uint32_t *indices;
        size_t vertex_bytes;
        size_t index_bytes;

        line_end = strchr(line_start, '\n');
        if (!line_end) {
            line_end = line_start + strlen(line_start);
        }
        line_len = (size_t)(line_end - line_start);

        hb_buffer_clear_contents(font->hb_buffer);
        hb_buffer_add_utf8(font->hb_buffer, line_start, (int)line_len, 0, (int)line_len);
        hb_buffer_guess_segment_properties(font->hb_buffer);
        hb_shape(font->hb_font, font->hb_buffer, NULL, 0);

        infos = hb_buffer_get_glyph_infos(font->hb_buffer, &glyph_count);
        positions = hb_buffer_get_glyph_positions(font->hb_buffer, &glyph_count);

        if (glyph_count == 0) {
            if (*line_end == '\n') {
                line_start = line_end + 1;
                cursor_y += font->height * sy;
                continue;
            }
            break;
        }

        vertex_count = glyph_count * 4;
        index_count = glyph_count * 6;
        vertex_bytes = vertex_count * sizeof(TextVertex);
        index_bytes = index_count * sizeof(uint32_t);

        if (!font->vertex_buffer || vertex_bytes > font->vertex_capacity) {
            if (font->vertex_buffer) {
                graphics_destroybuffer(font->vertex_buffer);
            }
            font->vertex_buffer = graphics_createvertexbuffer(NULL, vertex_bytes);
            font->vertex_capacity = vertex_bytes;
        }
        if (!font->index_buffer || index_bytes > font->index_capacity) {
            if (font->index_buffer) {
                graphics_destroybuffer(font->index_buffer);
            }
            font->index_buffer = graphics_createindexbuffer(NULL, index_bytes);
            font->index_capacity = index_bytes;
        }

        if (!font->vertex_buffer || !font->index_buffer) {
            return;
        }

        vertices = (TextVertex *)malloc(vertex_bytes);
        indices = (uint32_t *)malloc(index_bytes);
        if (!vertices || !indices) {
            free(vertices);
            free(indices);
            return;
        }

        font_build_vertices(font, infos, positions, glyph_count, origin_x, cursor_y, sx, sy, vertices, indices, &index_count);

        graphics_updatebuffer(font->vertex_buffer, vertices, vertex_bytes);
        graphics_updatebuffer(font->index_buffer, indices, index_bytes);

        free(vertices);
        free(indices);

        if (font->pipeline) {
            graphics_bindpipeline(font->pipeline);
        }

        graphics_bindtexture(font->atlas, 0);
        graphics_draw_buffers(font->vertex_buffer, font->index_buffer, index_count, NULL, NULL);

        if (*line_end == '\n') {
            line_start = line_end + 1;
            cursor_y += font->height * sy;
        } else {
            break;
        }
    }
}

void font_begin_batch(Font *font)
{
    if (!font) {
        return;
    }
    
    for (size_t i = 0; i < font->batch.count; i++) {
        if (font->batch.commands[i].text) {
            free(font->batch.commands[i].text);
            font->batch.commands[i].text = NULL;
        }
    }
    
    font->batch.count = 0;
}

void font_batch_print(Font *font,
                      const char *text,
                      float x, float y,
                      float sx, float sy)
{
    if (!font || !text) {
        return;
    }
    
    if (font->batch.count >= font->batch.capacity) {
        size_t new_capacity = font->batch.capacity == 0 ? INITIAL_BATCH_CAPACITY : font->batch.capacity * 2;
        TextDrawCommand *new_commands = (TextDrawCommand *)realloc(
            font->batch.commands,
            new_capacity * sizeof(TextDrawCommand)
        );
        if (!new_commands) {
            return;
        }
        font->batch.commands = new_commands;
        font->batch.capacity = new_capacity;
    }
    
    TextDrawCommand *cmd = &font->batch.commands[font->batch.count++];
    cmd->text = strdup(text);
    cmd->x = x;
    cmd->y = y;
    cmd->sx = (sx == 0.0f) ? 1.0f : sx;
    cmd->sy = (sy == 0.0f) ? 1.0f : sy;
}

typedef struct {
    Font *font;
    ShapedLine *lines;
    size_t line_count;
} ShapeJobContext;

static void shape_text_job(void *context, uint32_t jobIndex)
{
    ShapeJobContext *ctx = (ShapeJobContext *)context;
    ShapedLine *line = &ctx->lines[jobIndex];
    hb_buffer_t *buffer = line->infos ? (hb_buffer_t *)line->infos : NULL;
    
    if (!buffer) {
        return;
    }
    
    hb_buffer_clear_contents(buffer);
    hb_buffer_add_utf8(buffer, line->text, (int)line->text_len, 0, (int)line->text_len);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(ctx->font->hb_font, buffer, NULL, 0);
    
    line->infos = hb_buffer_get_glyph_infos(buffer, &line->glyph_count);
    line->positions = hb_buffer_get_glyph_positions(buffer, &line->glyph_count);
}

typedef struct {
    Font *font;
    ShapedLine *lines;
    TextVertex *vertices;
    uint32_t *indices;
    size_t *vertex_offsets;
    size_t *index_offsets;
    size_t vertex_base;
} VertexBuildContext;

static void build_vertices_job(void *context, uint32_t jobIndex)
{
    VertexBuildContext *ctx = (VertexBuildContext *)context;
    ShapedLine *line = &ctx->lines[jobIndex];
    
    if (line->glyph_count == 0) {
        return;
    }
    
    size_t v_offset = ctx->vertex_offsets[jobIndex];
    size_t i_offset = ctx->index_offsets[jobIndex];
    size_t index_count;
    
    font_build_vertices(ctx->font,
                       line->infos,
                       line->positions,
                       line->glyph_count,
                       line->origin_x,
                       line->origin_y,
                       line->sx,
                       line->sy,
                       &ctx->vertices[v_offset],
                       &ctx->indices[i_offset],
                       &index_count);
    
    for (size_t i = 0; i < index_count; i++) {
        ctx->indices[i_offset + i] += (uint32_t)(ctx->vertex_base + v_offset);
    }
}

void font_end_batch(Font *font)
{
    if (!font || font->batch.count == 0) {
        return;
    }
    
    size_t total_vertices = 0;
    size_t total_indices = 0;
    
    for (size_t cmd_idx = 0; cmd_idx < font->batch.count; cmd_idx++) {
        TextDrawCommand *cmd = &font->batch.commands[cmd_idx];
        if (!cmd->text) {
            continue;
        }
        
        const char *line_start = cmd->text;
        
        while (*line_start) {
            const char *line_end = strchr(line_start, '\n');
            if (!line_end) {
                line_end = line_start + strlen(line_start);
            }
            
            size_t line_len = (size_t)(line_end - line_start);
            
            hb_buffer_clear_contents(font->hb_buffer);
            hb_buffer_add_utf8(font->hb_buffer, line_start, (int)line_len, 0, (int)line_len);
            hb_buffer_guess_segment_properties(font->hb_buffer);
            hb_shape(font->hb_font, font->hb_buffer, NULL, 0);
            
            unsigned int glyph_count = 0;
            hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(font->hb_buffer, &glyph_count);
            hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(font->hb_buffer, &glyph_count);
            
            total_vertices += glyph_count * 4;
            total_indices += glyph_count * 6;
            
            if (*line_end == '\n') {
                line_start = line_end + 1;
            } else {
                break;
            }
        }
    }
    
    if (total_vertices == 0) {
        return;
    }
    
    size_t vertex_bytes = total_vertices * sizeof(TextVertex);
    size_t index_bytes = total_indices * sizeof(uint32_t);
    
    if (vertex_bytes > font->batch_vertex_capacity) {
        font->batch_vertices = (TextVertex *)realloc(font->batch_vertices, vertex_bytes);
        font->batch_vertex_capacity = vertex_bytes;
    }
    if (index_bytes > font->batch_index_capacity) {
        font->batch_indices = (uint32_t *)realloc(font->batch_indices, index_bytes);
        font->batch_index_capacity = index_bytes;
    }
    
    if (!font->batch_vertices || !font->batch_indices) {
        return;
    }
    
    size_t vertex_offset = 0;
    size_t index_offset = 0;
    
    for (size_t cmd_idx = 0; cmd_idx < font->batch.count; cmd_idx++) {
        TextDrawCommand *cmd = &font->batch.commands[cmd_idx];
        if (!cmd->text) {
            continue;
        }
        
        const char *line_start = cmd->text;
        float cursor_y = cmd->y;
        
        while (*line_start) {
            const char *line_end = strchr(line_start, '\n');
            if (!line_end) {
                line_end = line_start + strlen(line_start);
            }
            
            size_t line_len = (size_t)(line_end - line_start);
            
            hb_buffer_clear_contents(font->hb_buffer);
            hb_buffer_add_utf8(font->hb_buffer, line_start, (int)line_len, 0, (int)line_len);
            hb_buffer_guess_segment_properties(font->hb_buffer);
            hb_shape(font->hb_font, font->hb_buffer, NULL, 0);
            
            unsigned int glyph_count = 0;
            hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(font->hb_buffer, &glyph_count);
            hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(font->hb_buffer, &glyph_count);
            
            if (glyph_count > 0) {
                size_t line_index_count = 0;
                font_build_vertices(font, infos, positions, glyph_count,
                                   cmd->x, cursor_y, cmd->sx, cmd->sy,
                                   &font->batch_vertices[vertex_offset],
                                   &font->batch_indices[index_offset],
                                   &line_index_count);
                
                for (size_t i = 0; i < line_index_count; i++) {
                    font->batch_indices[index_offset + i] += (uint32_t)vertex_offset;
                }
                
                vertex_offset += glyph_count * 4;
                index_offset += line_index_count;
            }
            
            if (*line_end == '\n') {
                line_start = line_end + 1;
                cursor_y += font->height * cmd->sy;
            } else {
                break;
            }
        }
    }
    
    if (!font->vertex_buffer || vertex_bytes > font->vertex_capacity) {
        if (font->vertex_buffer) {
            graphics_destroybuffer(font->vertex_buffer);
        }
        font->vertex_buffer = graphics_createvertexbuffer(NULL, vertex_bytes);
        font->vertex_capacity = vertex_bytes;
    }
    if (!font->index_buffer || index_bytes > font->index_capacity) {
        if (font->index_buffer) {
            graphics_destroybuffer(font->index_buffer);
        }
        font->index_buffer = graphics_createindexbuffer(NULL, index_bytes);
        font->index_capacity = index_bytes;
    }
    
    if (!font->vertex_buffer || !font->index_buffer) {
        return;
    }
    
    graphics_updatebuffer(font->vertex_buffer, font->batch_vertices, vertex_bytes);
    graphics_updatebuffer(font->index_buffer, font->batch_indices, index_bytes);
    
    if (font->pipeline) {
        graphics_bindpipeline(font->pipeline);
    }
    graphics_bindtexture(font->atlas, 0);
    graphics_draw_buffers(font->vertex_buffer, font->index_buffer, total_indices, NULL, NULL);
}