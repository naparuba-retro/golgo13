/* Software UI engine for the launcher / options menu: comic-book primitives drawn by the CPU into a 1280x720 RGBA buffer. */
#ifndef GOLGO_UI_H
#define GOLGO_UI_H
#include <stdint.h>
#include <stddef.h>

#define UI_W 1280
#define UI_H 720
#define UI_RGBA(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))
#define UI_RGB(r, g, b) UI_RGBA(r, g, b, 255)

enum { UI_TITLE = 0, UI_BODY = 1 };                 /* font faces */
enum { UI_LEFT = 0, UI_CENTER = 1, UI_RIGHT = 2 };  /* horizontal anchors */

typedef struct { int w, h; uint32_t *px; } ui_image_t;   /* RGBA image owned by the caller (ui_image_load / ui_image_free) */

void ui_begin(uint32_t *px);                                         /* frame buffer UI_W x UI_H (RGBA bytes) to draw into */
int  ui_load_fonts(const char *const *dirs, int ndirs);              /* looks for font_title.bin / font_body.bin; 1 if both loaded */
int  ui_has_fonts(void);
ui_image_t *ui_image_load(const char *path);                         /* "G13I" w h + RGBA bytes; NULL if missing */
ui_image_t *ui_image_from_rgba(const uint8_t *rgba, int w, int h);
void ui_image_free(ui_image_t *im);

/* clip: a convex quad (or the whole screen) that every later primitive is restricted to */
void ui_clip_quad(const float *x, const float *y);
void ui_clip_none(void);

void ui_fill_rect(float x, float y, float w, float h, uint32_t c);
void ui_fill_quad(const float *x, const float *y, uint32_t c);
void ui_stroke_quad(const float *x, const float *y, float width, uint32_t c);
void ui_para(float x, float y, float w, float h, float skew, float *ox, float *oy);   /* parallelogram corners (top edge shifted right by skew) */
void ui_fill_para(float x, float y, float w, float h, float skew, uint32_t c);
void ui_stroke_para(float x, float y, float w, float h, float skew, float width, uint32_t c);
void ui_halftone(float x, float y, float w, float h, uint32_t c, float spacing, float angle, int gradient, float lo, float hi);   /* gradient: 0 flat 1 vertical 2 horizontal 3 radial */
void ui_speed_lines(float cx, float cy, uint32_t c, int n, unsigned seed, float inner);
void ui_image_draw(const ui_image_t *im, float x, float y, float w, float h, int gray);    /* stretched to the rectangle (use ui_clip_quad for skewed panels) */
void ui_tint(float x, float y, float w, float h, uint32_t c);                                /* alpha-blend a colour over a rectangle */

int  ui_text_width(int face, int size, const char *s);
int  ui_text_height(int face, int size);
void ui_text(int face, int size, float x, float y, uint32_t c, const char *s, int anchor);   /* y = top of the line */
void ui_text_outlined(int face, int size, float x, float y, uint32_t c, uint32_t outline, int ow, const char *s, int anchor);
int  ui_text_wrap(int face, int size, int maxw, const char *s, char lines[][160], int maxlines);   /* returns number of lines */
#endif
