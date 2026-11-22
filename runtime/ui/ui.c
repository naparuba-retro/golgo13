/* Software UI engine (see ui.h). Everything is drawn with simple scanline code; no external dependency. */
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t *g_px; static uint8_t g_mask[UI_W * UI_H]; static int g_clip;

void ui_begin(uint32_t *px) { g_px = px; g_clip = 0; }

static inline void blend(int x, int y, uint32_t c)
{
    if ((unsigned)x >= UI_W || (unsigned)y >= UI_H) return;
    unsigned a = c >> 24; if (g_clip) a = a * g_mask[y * UI_W + x] / 255;
    if (!a) return;
    uint32_t *d = &g_px[y * UI_W + x];
    if (a == 255) { *d = c | 0xff000000u; return; }
    uint32_t o = *d; unsigned ia = 255 - a;
    unsigned r = ((c & 255) * a + (o & 255) * ia) / 255, g = (((c >> 8) & 255) * a + ((o >> 8) & 255) * ia) / 255, b = (((c >> 16) & 255) * a + ((o >> 16) & 255) * ia) / 255;
    *d = r | (g << 8) | (b << 16) | 0xff000000u;
}

/* ---- convex quad scanline fill, into either the frame (c) or the clip mask ---- */
static void quad_scan(const float *x, const float *y, void (*row)(int y, int x0, int x1, void *u), void *u)
{
    float ymin = y[0], ymax = y[0]; for (int i = 1; i < 4; i++) { if (y[i] < ymin) ymin = y[i]; if (y[i] > ymax) ymax = y[i]; }
    int y0 = (int)ceilf(ymin - 0.5f), y1 = (int)floorf(ymax - 0.5f); if (y0 < 0) y0 = 0; if (y1 >= UI_H) y1 = UI_H - 1;
    for (int yy = y0; yy <= y1; yy++) {
        float fy = yy + 0.5f, xl = 1e9f, xr = -1e9f;
        for (int i = 0; i < 4; i++) {
            float ax = x[i], ay = y[i], bx = x[(i + 1) & 3], by = y[(i + 1) & 3];
            if ((ay <= fy && by > fy) || (by <= fy && ay > fy)) { float t = (fy - ay) / (by - ay), px = ax + t * (bx - ax); if (px < xl) xl = px; if (px > xr) xr = px; }
        }
        if (xl > xr) continue;
        int a = (int)ceilf(xl - 0.5f), b = (int)floorf(xr - 0.5f); if (a < 0) a = 0; if (b >= UI_W) b = UI_W - 1;
        if (a <= b) row(yy, a, b, u);
    }
}
static void row_fill(int y, int x0, int x1, void *u) { uint32_t c = *(uint32_t *)u; for (int x = x0; x <= x1; x++) blend(x, y, c); }
static void row_mask(int y, int x0, int x1, void *u) { (void)u; memset(g_mask + y * UI_W + x0, 255, (size_t)(x1 - x0 + 1)); }

void ui_clip_quad(const float *x, const float *y) { memset(g_mask, 0, sizeof g_mask); quad_scan(x, y, row_mask, NULL); g_clip = 1; }
void ui_clip_none(void) { g_clip = 0; }
void ui_fill_quad(const float *x, const float *y, uint32_t c) { quad_scan(x, y, row_fill, &c); }
void ui_fill_rect(float x, float y, float w, float h, uint32_t c) { float X[4] = { x, x + w, x + w, x }, Y[4] = { y, y, y + h, y + h }; ui_fill_quad(X, Y, c); }
void ui_para(float x, float y, float w, float h, float s, float *ox, float *oy)
{ ox[0] = x + s; oy[0] = y; ox[1] = x + w; oy[1] = y; ox[2] = x + w - s; oy[2] = y + h; ox[3] = x; oy[3] = y + h; }
void ui_fill_para(float x, float y, float w, float h, float s, uint32_t c) { float X[4], Y[4]; ui_para(x, y, w, h, s, X, Y); ui_fill_quad(X, Y, c); }
void ui_stroke_quad(const float *x, const float *y, float width, uint32_t c)
{
    for (int i = 0; i < 4; i++) {
        float ax = x[i], ay = y[i], bx = x[(i + 1) & 3], by = y[(i + 1) & 3], dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy); if (l < 0.01f) continue;
        float nx = -dy / l * width / 2, ny = dx / l * width / 2, ex = dx / l * width / 2, ey = dy / l * width / 2;
        float X[4] = { ax - ex + nx, bx + ex + nx, bx + ex - nx, ax - ex - nx }, Y[4] = { ay - ey + ny, by + ey + ny, by + ey - ny, ay - ey - ny };
        ui_fill_quad(X, Y, c);
    }
}
void ui_stroke_para(float x, float y, float w, float h, float s, float width, uint32_t c) { float X[4], Y[4]; ui_para(x, y, w, h, s, X, Y); ui_stroke_quad(X, Y, width, c); }
void ui_tint(float x, float y, float w, float h, uint32_t c) { ui_fill_rect(x, y, w, h, c); }

static void disc(float cx, float cy, float r, uint32_t c)
{
    int x0 = (int)floorf(cx - r), x1 = (int)ceilf(cx + r), y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
        if (d <= r - 0.5f) blend(x, y, c);
        else if (d < r + 0.5f) { uint32_t a = (uint32_t)((c >> 24) * (r + 0.5f - d)); blend(x, y, (c & 0xffffffu) | (a << 24)); }
    }
}
void ui_halftone(float x, float y, float w, float h, uint32_t c, float sp, float angle, int grad, float lo, float hi)
{
    float ca = cosf(angle * 3.14159265f / 180), sa = sinf(angle * 3.14159265f / 180), cx = x + w / 2, cy = y + h / 2, far = sqrtf(w * w + h * h) / 2;
    int R = (int)(far / sp) + 2;
    for (int i = -R; i <= R; i++) for (int j = -R; j <= R; j++) {
        float px = (i * ca - j * sa) * sp + cx, py = (i * sa + j * ca) * sp + cy;
        if (px < x - sp || px > x + w + sp || py < y - sp || py > y + h + sp) continue;
        float t = grad == 1 ? (py - y) / h : grad == 2 ? (px - x) / w : grad == 3 ? sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy)) / far : 1.0f;
        if (t < 0) t = 0; if (t > 1) t = 1; t = lo + (hi - lo) * t;
        float r = sp * 0.62f * sqrtf(t); if (r > 0.6f) disc(px, py, r, c);
    }
}
void ui_speed_lines(float cx, float cy, uint32_t c, int n, unsigned seed, float inner)
{
    unsigned s = seed * 2654435761u + 1; float far = sqrtf((float)(UI_W * UI_W + UI_H * UI_H));
    #define RND() (s = s * 1664525u + 1013904223u, (s >> 8) / 16777216.0f)
    for (int i = 0; i < n; i++) {
        float a = RND() * 6.2831853f, wid = 0.004f + RND() * 0.016f, r0 = far * (inner + RND() * 0.25f);
        float X[4] = { cx + cosf(a) * r0, cx + cosf(a - wid) * far, cx + cosf(a + wid) * far, cx + cosf(a) * r0 + 0.5f }, Y[4] = { cy + sinf(a) * r0, cy + sinf(a - wid) * far, cy + sinf(a + wid) * far, cy + sinf(a) * r0 + 0.5f };
        ui_fill_quad(X, Y, c);
    }
}

/* ---- images ---- */
ui_image_t *ui_image_from_rgba(const uint8_t *rgba, int w, int h)
{
    ui_image_t *im = (ui_image_t *)malloc(sizeof *im); if (!im) return NULL;
    im->w = w; im->h = h; im->px = (uint32_t *)malloc((size_t)w * h * 4); if (!im->px) { free(im); return NULL; }
    memcpy(im->px, rgba, (size_t)w * h * 4); return im;
}
ui_image_t *ui_image_load(const char *path)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL; uint32_t hd[3]; ui_image_t *im = NULL;
    if (fread(hd, 4, 3, f) == 3 && hd[0] == 0x49333147u && hd[1] > 0 && hd[1] <= 4096 && hd[2] > 0 && hd[2] <= 4096) {
        size_t n = (size_t)hd[1] * hd[2] * 4; uint8_t *b = (uint8_t *)malloc(n);
        if (b && fread(b, 1, n, f) == n) im = ui_image_from_rgba(b, (int)hd[1], (int)hd[2]); free(b);
    }
    fclose(f); return im;
}
void ui_image_free(ui_image_t *im) { if (im) { free(im->px); free(im); } }
void ui_image_draw(const ui_image_t *im, float x, float y, float w, float h, int gray)
{
    if (!im || w < 1 || h < 1) return;
    static int xmap[UI_W]; int x0 = (int)x, x1 = (int)(x + w), y0 = (int)y, y1 = (int)(y + h); if (x1 - x0 > UI_W) x1 = x0 + UI_W;
    for (int xx = x0; xx < x1; xx++) { int sx = (int)((xx - x) * im->w / w); xmap[xx - x0] = sx < 0 ? 0 : sx >= im->w ? im->w - 1 : sx; }
    for (int yy = y0; yy < y1; yy++) {
        int sy = (int)((yy - y) * im->h / h); if (sy < 0) sy = 0; if (sy >= im->h) sy = im->h - 1;
        const uint32_t *row = im->px + sy * im->w;
        if (!gray && !g_clip && yy >= 0 && yy < UI_H && x0 >= 0 && x1 <= UI_W) {                  /* fast path: opaque pixels are copied */
            uint32_t *d = g_px + yy * UI_W + x0;
            for (int xx = x0; xx < x1; xx++) { uint32_t p = row[xmap[xx - x0]]; if ((p >> 24) == 255) d[xx - x0] = p; else blend(xx, yy, p); }
            continue;
        }
        for (int xx = x0; xx < x1; xx++) {
            uint32_t p = row[xmap[xx - x0]];
            if (gray) { unsigned g = ((p & 255) * 30 + ((p >> 8) & 255) * 59 + ((p >> 16) & 255) * 11) / 100; p = (p & 0xff000000u) | g | (g << 8) | (g << 16); }
            blend(xx, yy, p);
        }
    }
}

/* ---- fonts: baked by tools/ui/bake_font.py ---- */
typedef struct { uint32_t code; int16_t adv, xo, yo; uint16_t w, h; uint32_t off; } glyph_t;
typedef struct { int px, asc, desc, ng; glyph_t *g; uint8_t *bmp; } fsize_t;
typedef struct { int n; fsize_t *s; int ok; } face_t;
static face_t g_face[2];

static int load_face(face_t *f, const char *path)
{
    FILE *fp = fopen(path, "rb"); if (!fp) return 0; uint32_t hd[2];
    if (fread(hd, 4, 2, fp) != 2 || hd[0] != 0x46333147u || hd[1] == 0 || hd[1] > 16) { fclose(fp); return 0; }
    f->n = (int)hd[1]; f->s = (fsize_t *)calloc(hd[1], sizeof(fsize_t));
    for (int i = 0; i < f->n; i++) {
        uint32_t m[5]; if (fread(m, 4, 5, fp) != 5) { fclose(fp); return 0; }
        fsize_t *s = &f->s[i]; s->px = (int)m[0]; s->asc = (int32_t)m[1]; s->desc = (int32_t)m[2]; s->ng = (int)m[3];
        s->g = (glyph_t *)malloc(sizeof(glyph_t) * m[3]); s->bmp = (uint8_t *)malloc(m[4] ? m[4] : 1);
        for (int k = 0; k < s->ng; k++) {
            uint8_t r[18]; if (fread(r, 1, 18, fp) != 18) { fclose(fp); return 0; }
            glyph_t *g = &s->g[k]; memcpy(&g->code, r, 4); memcpy(&g->adv, r + 4, 2); memcpy(&g->xo, r + 6, 2); memcpy(&g->yo, r + 8, 2);
            memcpy(&g->w, r + 10, 2); memcpy(&g->h, r + 12, 2); memcpy(&g->off, r + 14, 4);
        }
        if (fread(s->bmp, 1, m[4], fp) != m[4]) { fclose(fp); return 0; }
    }
    fclose(fp); f->ok = 1; return 1;
}
int ui_load_fonts(const char *const *dirs, int nd)
{
    static const char *nm[2] = { "font_title.bin", "font_body.bin" };
    for (int k = 0; k < 2; k++) {
        if (g_face[k].ok) continue;
        for (int i = 0; i < nd && !g_face[k].ok; i++) { char p[1300]; snprintf(p, sizeof p, "%s/%s", dirs[i], nm[k]); load_face(&g_face[k], p); }
    }
    return g_face[0].ok && g_face[1].ok;
}
int ui_has_fonts(void) { return g_face[0].ok && g_face[1].ok; }

static const fsize_t *pick(int face, int size)
{
    const face_t *f = &g_face[face]; if (!f->ok) return NULL;
    const fsize_t *best = &f->s[0];
    for (int i = 0; i < f->n; i++) if (f->s[i].px <= size || i == 0) best = &f->s[i];
    return best;
}
static const glyph_t *find_glyph(const fsize_t *s, uint32_t c)
{
    for (int i = 0; i < s->ng; i++) if (s->g[i].code == c) return &s->g[i];
    for (int i = 0; i < s->ng; i++) if (s->g[i].code == '?') return &s->g[i];
    return NULL;
}
static uint32_t next_cp(const char **p)                      /* UTF-8 decode */
{
    const unsigned char *s = (const unsigned char *)*p; uint32_t c = *s++;
    if (c >= 0xf0) { c = ((c & 7) << 18) | ((s[0] & 63) << 12) | ((s[1] & 63) << 6) | (s[2] & 63); s += 3; }
    else if (c >= 0xe0) { c = ((c & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63); s += 2; }
    else if (c >= 0xc0) { c = ((c & 31) << 6) | (s[0] & 63); s += 1; }
    *p = (const char *)s; return c;
}
int ui_text_height(int face, int size) { const fsize_t *s = pick(face, size); return s ? s->asc + s->desc : size; }
int ui_text_width(int face, int size, const char *str)
{
    const fsize_t *s = pick(face, size); if (!s) return (int)strlen(str) * size / 2; int w = 0;
    while (*str) { const glyph_t *g = find_glyph(s, next_cp(&str)); if (g) w += g->adv; }
    return w;
}
void ui_text(int face, int size, float x, float y, uint32_t c, const char *str, int anchor)
{
    const fsize_t *s = pick(face, size); if (!s) return;
    if (anchor == UI_CENTER) x -= ui_text_width(face, size, str) / 2.0f; else if (anchor == UI_RIGHT) x -= ui_text_width(face, size, str);
    int pen = (int)x, base = (int)y + s->asc;
    while (*str) {
        const glyph_t *g = find_glyph(s, next_cp(&str)); if (!g) continue;
        for (int gy = 0; gy < g->h; gy++) for (int gx = 0; gx < g->w; gx++) {
            unsigned a = s->bmp[g->off + gy * g->w + gx]; if (!a) continue;
            blend(pen + g->xo + gx, base + g->yo + gy, (c & 0xffffffu) | ((((c >> 24) * a) / 255) << 24));
        }
        pen += g->adv;
    }
}
void ui_text_outlined(int face, int size, float x, float y, uint32_t c, uint32_t oc, int ow, const char *str, int anchor)
{
    for (int dy = -ow; dy <= ow; dy++) for (int dx = -ow; dx <= ow; dx++) if (dx * dx + dy * dy <= ow * ow + 1) ui_text(face, size, x + dx, y + dy, oc, str, anchor);
    ui_text(face, size, x, y, c, str, anchor);
}
int ui_text_wrap(int face, int size, int maxw, const char *str, char lines[][160], int maxl)
{
    int n = 0; char cur[160] = ""; const char *p = str;
    while (*p && n < maxl) {
        char word[100]; int k = 0; while (*p && *p != ' ' && k < 99) word[k++] = *p++; word[k] = 0; while (*p == ' ') p++;
        char t[260]; snprintf(t, sizeof t, "%s%s%s", cur, cur[0] ? " " : "", word);
        if (cur[0] && ui_text_width(face, size, t) > maxw) { snprintf(lines[n++], 160, "%s", cur); snprintf(cur, sizeof cur, "%s", word); } else snprintf(cur, sizeof cur, "%.159s", t);
    }
    if (cur[0] && n < maxl) snprintf(lines[n++], 160, "%s", cur);
    return n;
}
