/* Copyright Planimeter. All Rights Reserved. */

#include "text.h"
#include <stdlib.h>
#include <string.h>

struct Text {
    Font *font;
    char *textstring;
};

Text *text_create(Font *font, const char *textstring)
{
    Text *text = (Text *)calloc(1, sizeof(Text));
    if (!text) {
        return NULL;
    }

    text->font = font;
    text_set(text, textstring);
    return text;
}

void text_destroy(Text *text)
{
    if (!text) {
        return;
    }

    free(text->textstring);
    free(text);
}

void text_set(Text *text, const char *textstring)
{
    if (!text) {
        return;
    }

    free(text->textstring);
    text->textstring = NULL;

    if (textstring) {
        size_t len = strlen(textstring);
        text->textstring = (char *)malloc(len + 1);
        if (text->textstring) {
            memcpy(text->textstring, textstring, len + 1);
        }
    }
}

void text_set_font(Text *text, Font *font)
{
    if (!text) {
        return;
    }

    text->font = font;
}

Font *text_get_font(Text *text)
{
    if (!text) {
        return NULL;
    }

    return text->font;
}

void text_draw(Text *text,
               float x, float y,
               float r,
               float sx, float sy,
               float ox, float oy,
               float kx, float ky)
{
    (void)r;
    (void)ox;
    (void)oy;
    (void)kx;
    (void)ky;

    if (!text || !text->font) {
        return;
    }

    if (sx == 0.0f) {
        sx = 1.0f;
    }
    if (sy == 0.0f) {
        sy = 1.0f;
    }

    font_batch_print(text->font, text->textstring ? text->textstring : "", x, y, sx, sy);
}