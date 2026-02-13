/* Copyright Planimeter. All Rights Reserved. */

#ifndef TEXT_H
#define TEXT_H

#include "font.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Text Text;

Text *text_create(Font *font, const char *textstring);
void  text_destroy(Text *text);
void  text_set(Text *text, const char *textstring);
void  text_set_font(Text *text, Font *font);
Font *text_get_font(Text *text);
void  text_draw(Text *text,
                float x, float y,
                float r,
                float sx, float sy,
                float ox, float oy,
                float kx, float ky);

#ifdef __cplusplus
}
#endif

#endif /* TEXT_H */
