/* Copyright Planimeter. All Rights Reserved. */

#ifndef FONT_H
#define FONT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Font Font;

Font *font_create(const char *filepath, int size);
void  font_destroy(Font *font);
int   font_get_width(Font *font, const char *text);
int   font_get_height(Font *font);
void  font_print(Font *font,
                 const char *text,
                 float x, float y,
                 float r,
                 float sx, float sy,
                 float ox, float oy,
                 float kx, float ky);

#ifdef __cplusplus
}
#endif

#endif /* FONT_H */
