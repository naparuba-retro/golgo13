/* Software GPU: GP0 packet processor, rasteriser, VRAM transfers, display read-out. See gpu.h. */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "gpu.h"
#include "inflate.h"

typedef struct { int x, y, r, g, b, u, v; } vtx_t;

static struct {
    uint16_t vram[VRAM_W * VRAM_H];
    uint32_t status, info;
    /* GP1 / display */
    int disp_x, disp_y, hstart, hend, vstart, vend;
    /* draw environment */
    int da_x1, da_y1, da_x2, da_y2, off_x, off_y;
    int tx, ty, abr, tp, flip_x, flip_y;
    int tw_mask_x, tw_mask_y, tw_off_x, tw_off_y;   /* texture window in pixels (mask = bits replaced by offset) */
    int draw_stp, check_stp;
    /* GP0 FIFO */
    uint32_t fifo[40]; int fifo_n, fifo_need;
    int polyline; vtx_t pl_last; uint32_t pl_cmd; int pl_gour;
    /* CPU -> VRAM */
    int xfer, xfer_x, xfer_y, xfer_w, xfer_h, xfer_cx, xfer_cy;
    /* VRAM -> CPU */
    int rd, rd_x, rd_y, rd_w, rd_h, rd_cx, rd_cy;
    uint64_t words, prims;
} S;

#include <stdio.h>
int gpu_trace_loads; uint64_t gpu_frame_no; uint32_t gpu_src_addr;
int gpu_nodraw; int gpu_log; int gpu_trace_x = -1, gpu_trace_y = -1;
uint16_t *gpu_vram(void) { return S.vram; }
uint64_t gpu_stat_words(void) { return S.words; }
uint64_t gpu_stat_prims(void) { return S.prims; }

static inline int sext(int v, int bits) { int m = 1 << (bits - 1); v &= (1 << bits) - 1; return (v ^ m) - m; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

void gpu_reset(void)
{
    S.status = 0x14802000; S.info = 0;
    S.disp_x = S.disp_y = 0; S.hstart = 0x260; S.hend = 0xc60; S.vstart = 0x10; S.vend = 0x100;
    S.da_x1 = S.da_y1 = 0; S.da_x2 = S.da_y2 = 0; S.off_x = S.off_y = 0;
    S.tx = S.ty = S.abr = S.tp = S.flip_x = S.flip_y = 0;
    S.tw_mask_x = S.tw_mask_y = S.tw_off_x = S.tw_off_y = 0;
    S.draw_stp = S.check_stp = 0;
    S.fifo_n = S.fifo_need = 0; S.polyline = 0; S.xfer = S.rd = 0;
}

/* ------------------------------------------------------------------ draw mode (System 12 layout) */
static void decode_tpage(uint32_t tpage)
{
    S.status = (S.status & 0xffff7800u) | (tpage & 0x7ff) | ((tpage & 0x800) << 4);
    S.tx = (int)(tpage & 0x0f) << 6;
    S.ty = (int)(((tpage & 0x10) << 4) | ((tpage & 0x800) >> 2));
    S.abr = (int)((tpage & 0x60) >> 5);
    S.tp = (int)((tpage & 0x180) >> 7);
    S.flip_x = (int)((tpage & 0x1000) >> 12);
    S.flip_y = (int)((tpage & 0x2000) >> 13);
}

/* ------------------------------------------------------------------ pixel output */
static inline void put_pixel(int x, int y, int r, int g, int b, int srcbit, int semi)
{
    uint16_t *p = &S.vram[((y & 1023) << 10) | (x & 1023)];
    uint16_t dst = *p;
    if (x == gpu_trace_x && y == gpu_trace_y) fprintf(stderr, "PIX prim#%llu (%d,%d) rgb=%d,%d,%d srcbit=%d semi=%d dst=%04x\n", (unsigned long long)S.prims, x, y, r, g, b, srcbit, semi, dst);
    if (S.check_stp && (dst & 0x8000)) return;
    int out_bit = srcbit;
    if (semi) {
        int dr = dst & 31, dg = (dst >> 5) & 31, db = (dst >> 10) & 31;
        switch (S.abr) {
        case 0: r = (dr + r) >> 1; g = (dg + g) >> 1; b = (db + b) >> 1; break;
        case 1: r += dr; g += dg; b += db; break;
        case 2: r = dr - r; g = dg - g; b = db - b; break;
        default: r = dr + (r >> 2); g = dg + (g >> 2); b = db + (b >> 2); break;
        }
        r = r < 0 ? 0 : r > 31 ? 31 : r; g = g < 0 ? 0 : g > 31 ? 31 : g; b = b < 0 ? 0 : b > 31 ? 31 : b;
    }
    *p = (uint16_t)(r | (g << 5) | (b << 10) | ((S.draw_stp || out_bit) ? 0x8000 : 0));
}

typedef struct { int tex, semi, gour, raw, clutx, cluty; } prim_t;

/* ------------------------------------------------------------------ texture sampling */
typedef struct {
    int tex, semi, gour, raw;
    int tp, tx, ty, wmx, wmy, wox, woy;      /* texture page / window */
    uint16_t clut[256];
    int umin, umax, vmin, vmax;              /* clamp range of the interpolated coordinates */
    int ident;                               /* modulation colour is neutral (128,128,128) or raw texture: output = texel */
} ctx_t;

static void ctx_setup(ctx_t *c, int tex, int semi, int gour, int raw, int clutx, int cluty)
{
    c->tex = tex; c->semi = semi; c->gour = gour; c->raw = raw;
    c->tp = S.tp; c->tx = S.tx; c->ty = S.ty;
    c->wmx = S.tw_mask_x; c->wmy = S.tw_mask_y; c->wox = S.tw_off_x & S.tw_mask_x; c->woy = S.tw_off_y & S.tw_mask_y;
    if (tex && S.tp < 2) {                                  /* cache the palette */
        const uint16_t *row = &S.vram[cluty << 10]; int n = S.tp == 0 ? 16 : 256;
        for (int i = 0; i < n; i++) c->clut[i] = row[(clutx + i) & 1023];
    }
}

static inline uint16_t texel(const ctx_t *c, int u, int v)
{
    u &= 255; v &= 255;
    if (c->wmx | c->wmy) { u = (u & ~c->wmx) | c->wox; v = (v & ~c->wmy) | c->woy; }
    const uint16_t *r = &S.vram[((c->ty + v) & 1023) << 10];
    switch (c->tp) {
    case 0: return c->clut[(r[(c->tx + (u >> 2)) & 1023] >> ((u & 3) << 2)) & 0xf];
    case 1: return c->clut[(r[(c->tx + (u >> 1)) & 1023] >> ((u & 1) << 3)) & 0xff];
    default: return r[(c->tx + u) & 1023];
    }
}

/* ------------------------------------------------------------------ span drawing */
#define FX 16
static inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

typedef struct { int r, g, b, u, v, dr, dg, db, du, dv; } span_t;   /* 16.16 fixed-point attributes at xl and their x-gradients */

uint64_t gpu_px_flat, gpu_px_gour, gpu_px_tex, gpu_px_slow, gpu_spans;
static void draw_span(int y, int xl, int xr, span_t s, const ctx_t *c)
{
    uint16_t *row = &S.vram[(y & 1023) << 10];
    const int stp = S.draw_stp ? 0x8000 : 0;
    const int fast = !c->semi && !S.check_stp;
    { uint64_t n = (uint64_t)(xr - xl + 1); gpu_spans++; if (!fast) gpu_px_slow += n; else if (c->tex) gpu_px_tex += n; else if (c->gour) gpu_px_gour += n; else gpu_px_flat += n; }
    if (!c->tex) {
        if (fast) {
            if (!c->gour) {
                uint16_t col = (uint16_t)((clamp255(s.r >> FX) >> 3) | ((clamp255(s.g >> FX) >> 3) << 5) | ((clamp255(s.b >> FX) >> 3) << 10) | stp);
                for (int x = xl; x <= xr; x++) row[x] = col;
            } else {
                for (int x = xl; x <= xr; x++) {
                    row[x] = (uint16_t)((clamp255(s.r >> FX) >> 3) | ((clamp255(s.g >> FX) >> 3) << 5) | ((clamp255(s.b >> FX) >> 3) << 10) | stp);
                    s.r += s.dr; s.g += s.dg; s.b += s.db;
                }
            }
            return;
        }
        for (int x = xl; x <= xr; x++) {
            put_pixel(x, y, clamp255(s.r >> FX) >> 3, clamp255(s.g >> FX) >> 3, clamp255(s.b >> FX) >> 3, 0, c->semi);
            s.r += s.dr; s.g += s.dg; s.b += s.db;
        }
        return;
    }
    if (fast && c->ident && !(c->wmx | c->wmy)) {            /* neutral modulation: copy texels (skip index 0) */
        int u = s.u, v = s.v; const int du = s.du, dv = s.dv, umin = c->umin, umax = c->umax;
        #define CLAMPU(x) ((x) < umin ? umin : (x) > umax ? umax : (x))
        if (dv == 0) {                                      /* v constant along the span: fixed texture row */
            int vv = v >> FX; vv = vv < c->vmin ? c->vmin : vv > c->vmax ? c->vmax : vv;
            const uint16_t *r = &S.vram[((c->ty + (vv & 255)) & 1023) << 10];
            if (c->tp == 0) {
                for (int x = xl; x <= xr; x++, u += du) { int uu = CLAMPU(u >> FX) & 255; uint16_t t = c->clut[(r[(c->tx + (uu >> 2)) & 1023] >> ((uu & 3) << 2)) & 0xf]; if (t) row[x] = (uint16_t)(t | stp); }
            } else if (c->tp == 1) {
                for (int x = xl; x <= xr; x++, u += du) { int uu = CLAMPU(u >> FX) & 255; uint16_t t = c->clut[(r[(c->tx + (uu >> 1)) & 1023] >> ((uu & 1) << 3)) & 0xff]; if (t) row[x] = (uint16_t)(t | stp); }
            } else {
                for (int x = xl; x <= xr; x++, u += du) { int uu = CLAMPU(u >> FX) & 255; uint16_t t = r[(c->tx + uu) & 1023]; if (t) row[x] = (uint16_t)(t | stp); }
            }
        } else {
            for (int x = xl; x <= xr; x++, u += du, v += dv) {
                int uu = CLAMPU(u >> FX), vv = v >> FX; vv = vv < c->vmin ? c->vmin : vv > c->vmax ? c->vmax : vv;
                uint16_t t = texel(c, uu, vv); if (t) row[x] = (uint16_t)(t | stp);
            }
        }
        #undef CLAMPU
        return;
    }
    for (int x = xl; x <= xr; x++) {
        int u = (s.u >> FX), v = (s.v >> FX);
        u = u < c->umin ? c->umin : u > c->umax ? c->umax : u; v = v < c->vmin ? c->vmin : v > c->vmax ? c->vmax : v;
        uint16_t t = texel(c, u, v);
        if (t) {
            int sr = c->raw ? 128 : clamp255(s.r >> FX), sg = c->raw ? 128 : clamp255(s.g >> FX), sb = c->raw ? 128 : clamp255(s.b >> FX);
            int r = ((t & 31) * sr) >> 7, g = (((t >> 5) & 31) * sg) >> 7, b = (((t >> 10) & 31) * sb) >> 7;
            r = r > 31 ? 31 : r; g = g > 31 ? 31 : g; b = b > 31 ? 31 : b;
            if (fast) row[x] = (uint16_t)(r | (g << 5) | (b << 10) | (t & 0x8000) | stp);
            else put_pixel(x, y, r, g, b, (t >> 15) & 1, c->semi && (t & 0x8000));
        }
        s.r += s.dr; s.g += s.dg; s.b += s.db; s.u += s.du; s.v += s.dv;
    }
}

/* ------------------------------------------------------------------ triangles */
static inline int64_t floor_div(int64_t a, int64_t b) { int64_t q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
static inline int64_t ceil_div(int64_t a, int64_t b) { return -floor_div(-a, b); }
static inline int top_left(int ax, int ay, int bx, int by)    /* edge a->b in the oriented (area>0) triangle */
{
    int dx = bx - ax, dy = by - ay;
    return (dy < 0) || (dy == 0 && dx > 0);
}

static void tri(vtx_t v0, vtx_t v1, vtx_t v2, const prim_t *p)
{
    int64_t area = (int64_t)(v1.x - v0.x) * (v2.y - v0.y) - (int64_t)(v2.x - v0.x) * (v1.y - v0.y);
    if (area == 0) return;
    if (area < 0) { vtx_t t = v1; v1 = v2; v2 = t; area = -area; }
    int minx = imax(imin(v0.x, imin(v1.x, v2.x)), S.da_x1), maxx = imin(imax(v0.x, imax(v1.x, v2.x)), S.da_x2);
    int miny = imax(imin(v0.y, imin(v1.y, v2.y)), S.da_y1), maxy = imin(imax(v0.y, imax(v1.y, v2.y)), S.da_y2);
    if (minx > maxx || miny > maxy) return;
    /* edge functions w_i(x,y) = A_i*x + B_i*y + C_i ; w0 opposite v0 (edge v1->v2), w1 (v2->v0), w2 (v0->v1) */
    const vtx_t *ea[3] = { &v1, &v2, &v0 }, *eb[3] = { &v2, &v0, &v1 };
    int64_t A[3], B[3], C[3]; int thr[3];
    for (int i = 0; i < 3; i++) {
        int64_t dx = eb[i]->x - ea[i]->x, dy = eb[i]->y - ea[i]->y;
        A[i] = -dy; B[i] = dx; C[i] = -dx * ea[i]->y + dy * ea[i]->x;
        thr[i] = top_left(ea[i]->x, ea[i]->y, eb[i]->x, eb[i]->y) ? 0 : 1;
    }
    ctx_t cx; ctx_setup(&cx, p->tex, p->semi, p->gour, p->raw, p->clutx, p->cluty);
    cx.ident = p->raw || (!p->gour && v0.r == 128 && v0.g == 128 && v0.b == 128);
    /* attribute planes in 16.16 fixed point: a(x,y) = (w0*a0 + w1*a1 + w2*a2) / area */
    int64_t pa[5][3];    /* per attribute: d/dx, d/dy, value at (0,0) */
    const int a0[5] = { v0.r, v0.g, v0.b, v0.u, v0.v }, a1[5] = { v1.r, v1.g, v1.b, v1.u, v1.v }, a2[5] = { v2.r, v2.g, v2.b, v2.u, v2.v };
    int nattr = p->tex ? 5 : 3;
    if (!p->gour) nattr = p->tex ? 5 : 0;
    for (int k = 0; k < 5; k++) {
        if ((k < 3 && !p->gour) || (k >= 3 && !p->tex)) { pa[k][0] = pa[k][1] = 0; pa[k][2] = (int64_t)(k < 3 ? a0[k] : 0) * (1 << FX); continue; }
        pa[k][0] = ((A[0] * a0[k] + A[1] * a1[k] + A[2] * a2[k]) * (1 << FX)) / area;
        pa[k][1] = ((B[0] * a0[k] + B[1] * a1[k] + B[2] * a2[k]) * (1 << FX)) / area;
        pa[k][2] = ((C[0] * a0[k] + C[1] * a1[k] + C[2] * a2[k]) * (1 << FX)) / area;
    }
    (void)nattr;
    cx.umin = imin(v0.u, imin(v1.u, v2.u)); cx.umax = imax(v0.u, imax(v1.u, v2.u));
    cx.vmin = imin(v0.v, imin(v1.v, v2.v)); cx.vmax = imax(v0.v, imax(v1.v, v2.v));
    for (int y = miny; y <= maxy; y++) {
        int64_t xl = minx, xr = maxx;
        for (int i = 0; i < 3 && xl <= xr; i++) {
            int64_t Cy = B[i] * y + C[i], t = thr[i] - Cy;          /* need A*x >= t */
            if (A[i] == 0) { if (Cy < thr[i]) { xl = 1; xr = 0; } }
            else if (A[i] > 0) { int64_t lo = ceil_div(t, A[i]); if (lo > xl) xl = lo; }
            else { int64_t hi = floor_div(-t, -A[i]); if (hi < xr) xr = hi; }
        }
        if (xl > xr) continue;
        span_t s;
        s.dr = (int)pa[0][0]; s.dg = (int)pa[1][0]; s.db = (int)pa[2][0]; s.du = (int)pa[3][0]; s.dv = (int)pa[4][0];
        s.r = (int)(pa[0][2] + pa[0][0] * xl + pa[0][1] * y); s.g = (int)(pa[1][2] + pa[1][0] * xl + pa[1][1] * y); s.b = (int)(pa[2][2] + pa[2][0] * xl + pa[2][1] * y);
        s.u = (int)(pa[3][2] + pa[3][0] * xl + pa[3][1] * y); s.v = (int)(pa[4][2] + pa[4][0] * xl + pa[4][1] * y);
        draw_span(y, (int)xl, (int)xr, s, &cx);
    }
}

static int cull(const vtx_t *a, const vtx_t *b) { return abs(a->x - b->x) > 1023 || abs(a->y - b->y) > 1023; }

const char *gpu_texdump_dir; uint64_t gpu_texdump_from, gpu_texdump_to = ~0ull;
/* Debug: save every distinct texture rectangle the game samples (decoded with its palette as RGBA) into gpu_texdump_dir. */
static void tex_record(int tp, int tx, int ty, int clutx, int cluty, int u0, int v0, int u1, int v1)
{
    static uint64_t *seen; static uint8_t *buf;
    if (!gpu_texdump_dir || gpu_frame_no < gpu_texdump_from || gpu_frame_no > gpu_texdump_to || tp > 2) return;
    if (u0 > u1) { int t = u0; u0 = u1; u1 = t; }
    if (v0 > v1) { int t = v0; v0 = v1; v1 = t; }
    if (u0 < 0) u0 = 0; if (v0 < 0) v0 = 0; if (u1 > 255) u1 = 255; if (v1 > 255) v1 = 255;
    int w = u1 - u0 + 1, h = v1 - v0 + 1; if (w < 2 || h < 2 || w * h < 24) return;
    if (!seen) { seen = (uint64_t *)calloc(1u << 21, 8); buf = (uint8_t *)malloc(256 * 256 * 4); if (!seen || !buf) return; }
    uint16_t pal[256] = {0};
    if (tp < 2) { int n = tp == 0 ? 16 : 256; for (int i = 0; i < n; i++) pal[i] = S.vram[((cluty & 1023) << 10) | ((clutx + i) & 1023)]; }
    uint64_t hsh = 1469598103934665603ull; uint8_t *o = buf;
    for (int v = v0; v <= v1; v++) for (int u = u0; u <= u1; u++) {
        int row = (ty + v) & 1023; uint16_t c;
        if (tp == 0) { uint16_t wd = S.vram[(row << 10) | ((tx + (u >> 2)) & 1023)]; c = pal[(wd >> ((u & 3) * 4)) & 15]; }
        else if (tp == 1) { uint16_t wd = S.vram[(row << 10) | ((tx + (u >> 1)) & 1023)]; c = pal[(wd >> ((u & 1) * 8)) & 255]; }
        else c = S.vram[(row << 10) | ((tx + u) & 1023)];
        o[0] = (uint8_t)((c & 31) << 3 | (c & 31) >> 2); o[1] = (uint8_t)((c >> 5 & 31) << 3 | (c >> 5 & 31) >> 2); o[2] = (uint8_t)((c >> 10 & 31) << 3 | (c >> 10 & 31) >> 2); o[3] = c == 0 ? 0 : 255;
        for (int k = 0; k < 4; k++) { hsh ^= o[k]; hsh *= 1099511628211ull; }
        o += 4;
    }
    hsh ^= (uint64_t)w << 32 | (uint64_t)h; hsh *= 1099511628211ull;
    uint32_t slot = (uint32_t)(hsh >> 20) & ((1u << 21) - 1);
    while (seen[slot]) { if (seen[slot] == hsh) return; slot = (slot + 1) & ((1u << 21) - 1); }
    seen[slot] = hsh;
    char path[1200]; snprintf(path, sizeof path, "%s/t%016llx.rgba", gpu_texdump_dir, (unsigned long long)hsh);
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "RGBA %d %d\n", w, h); fwrite(buf, 4, (size_t)w * h, f); fclose(f);
    snprintf(path, sizeof path, "%s/index.txt", gpu_texdump_dir); f = fopen(path, "ab");
    if (f) { fprintf(f, "t%016llx f=%llu tp=%d page=%d,%d clut=%d,%d uv=%d,%d-%d,%d\n", (unsigned long long)hsh, (unsigned long long)gpu_frame_no, tp, tx, ty, clutx, cluty, u0, v0, u1, v1); fclose(f); }
}

/* ------------------------------------------------------------------ text overlays (translation)
 * An overlay replaces a picture the game draws from 32x32 4-bit tiles (e.g. a caption) by a ready-made RGBA bitmap. The tiles of the
 * original picture are recognised by the hash of their pixels in VRAM, their quads are not drawn, and the bitmap is composited into
 * VRAM at the end of the frame (so it lands on top of whatever the game drew around it, e.g. the caption box). */
typedef struct { uint64_t hash; int ovl, ex, ey, meta; } okey_t;
typedef struct { int w, h, nosup, sample, sx, sy, ref[3]; uint8_t *rgba; } ovl_t;
typedef struct { int ovl, ox, oy, x1, y1, x2, y2, bx0, by0, bx1, by1; } pend_t;   /* b*: screen box of the keyed tile / sprite (used when a picture is drawn from pieces that slide separately) */
static ovl_t *ovls; static int novl; static okey_t *otab; static uint32_t otab_n; static pend_t pend[64]; static int npend;
static uint8_t odims[256 * 256 / 8]; static int nrect_keys;

static uint64_t fnv64(const uint8_t *p, size_t n) { uint64_t h = 1469598103934665603ull; for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; } return h; }
static uint32_t rd32le_(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* File "G13O" v4: u32 version, u32 count; per overlay: u32 w, u32 h, u32 nkeys, u32 flags (bit0: paint over, keep drawing the game's own quads), nkeys * {u64 hash, i32 ex, i32 ey, u32 meta}, w*h*4 RGBA bytes.
 * meta = 0 for a 32x32 4-bit tile key (hash of the 512 tile bytes), or w | h<<16 for a sprite-rectangle key (hash of the pixel indexes of a w*h quad). */
int gpu_overlay_load(const char *path, char *msg, size_t msgsz)
{
    FILE *f = fopen(path, "rb"); if (!f) { snprintf(msg, msgsz, "cannot open %s", path); return -1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = (uint8_t *)malloc(sz > 0 ? (size_t)sz : 1);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); snprintf(msg, msgsz, "cannot read %s", path); return -1; }
    fclose(f);
    if (sz < 12 || memcmp(b, "G13O", 4) || rd32le_(b + 4) != 4) { free(b); snprintf(msg, msgsz, "%s is not a G13O v4 file (rebuild the translation)", path); return -1; }
    uint32_t count = rd32le_(b + 8); size_t p = 12; size_t total_keys = 0;
    ovls = (ovl_t *)calloc(count ? count : 1, sizeof(ovl_t));
    okey_t *keys = NULL; size_t cap = 0, nk = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (p + 16 > (size_t)sz) goto bad;
        uint32_t w = rd32le_(b + p), h = rd32le_(b + p + 4), nkey = rd32le_(b + p + 8), flags = rd32le_(b + p + 12); p += 16;
        uint32_t sxy[3] = { 0, 0, 0 }; if (flags & 2) { if (p + 12 > (size_t)sz) goto bad; sxy[0] = rd32le_(b + p); sxy[1] = rd32le_(b + p + 4); sxy[2] = rd32le_(b + p + 8); p += 12; }
        if (w == 0 || h == 0 || w > 1024 || h > 1024 || p + (size_t)nkey * 20 + ((flags & 4) ? 4 : (size_t)w * h * 4) > (size_t)sz) goto bad;
        for (uint32_t k = 0; k < nkey; k++, p += 20) {
            if (nk == cap) { cap = cap ? cap * 2 : 256; keys = (okey_t *)realloc(keys, cap * sizeof(okey_t)); }
            uint64_t hh = (uint64_t)rd32le_(b + p) | ((uint64_t)rd32le_(b + p + 4) << 32);
            keys[nk].hash = hh; keys[nk].ovl = (int)i; keys[nk].ex = (int)rd32le_(b + p + 8); keys[nk].ey = (int)rd32le_(b + p + 12); keys[nk].meta = (int)rd32le_(b + p + 16);
            if (keys[nk].meta) { int kw = keys[nk].meta & 0xffff, kh = (keys[nk].meta >> 16) & 0xffff; if (kw < 256 && kh < 256) { odims[(kh * 256 + kw) >> 3] |= (uint8_t)(1 << ((kh * 256 + kw) & 7)); nrect_keys++; } }
            nk++;
        }
        size_t csz = 0; if (flags & 4) { csz = rd32le_(b + p); p += 4; if (p + csz > (size_t)sz) goto bad; }
        ovls[i].w = (int)w; ovls[i].h = (int)h; ovls[i].nosup = (int)(flags & 1); ovls[i].sample = (int)((flags >> 1) & 1); ovls[i].sx = (int)sxy[0]; ovls[i].sy = (int)sxy[1];
        ovls[i].ref[0] = (int)((sxy[2] >> 16) & 255); ovls[i].ref[1] = (int)((sxy[2] >> 8) & 255); ovls[i].ref[2] = (int)(sxy[2] & 255); ovls[i].rgba = (uint8_t *)malloc((size_t)w * h * 4);
        if (flags & 4) { size_t got = 0; if (rt_inflate(b + p, csz, ovls[i].rgba, (size_t)w * h * 4, &got) || got != (size_t)w * h * 4) goto bad; p += csz; }
        else { memcpy(ovls[i].rgba, b + p, (size_t)w * h * 4); p += (size_t)w * h * 4; }
        total_keys += nkey;
    }
    novl = (int)count; otab_n = 1; while (otab_n < nk * 2 + 8) otab_n <<= 1;
    otab = (okey_t *)calloc(otab_n, sizeof(okey_t));
    for (size_t k = 0; k < nk; k++) { if (!keys[k].hash) keys[k].hash = 1; uint32_t s = (uint32_t)(keys[k].hash >> 11) & (otab_n - 1); while (otab[s].hash) s = (s + 1) & (otab_n - 1); otab[s] = keys[k]; }
    free(keys); free(b);
    snprintf(msg, msgsz, "overlay %s: %d picture(s), %zu tile keys", path, (int)count, total_keys);
    return (int)count;
bad:
    free(keys); free(b); snprintf(msg, msgsz, "%s is truncated or invalid", path); novl = 0; return -1;
}

/* Axis-aligned 1:1 textured quad -> its screen rectangle and texture origin. */
static int quad_rect(const vtx_t *v, int nv, int *x0, int *y0, int *w, int *h, int *u0, int *v0)
{
    if (nv != 4) return 0;
    int minx = v[0].x, maxx = v[0].x, miny = v[0].y, maxy = v[0].y, minu = v[0].u, maxu = v[0].u, minv = v[0].v, maxv = v[0].v;
    for (int i = 1; i < 4; i++) { if (v[i].x < minx) minx = v[i].x; if (v[i].x > maxx) maxx = v[i].x; if (v[i].y < miny) miny = v[i].y; if (v[i].y > maxy) maxy = v[i].y;
                                  if (v[i].u < minu) minu = v[i].u; if (v[i].u > maxu) maxu = v[i].u; if (v[i].v < minv) minv = v[i].v; if (v[i].v > maxv) maxv = v[i].v; }
    int ww = maxx - minx, hh = maxy - miny;
    if (ww <= 0 || hh <= 0 || ww > 255 || hh > 255 || maxu - minu != ww || maxv - minv != hh) return 0;
    for (int i = 0; i < 4; i++) {
        if ((v[i].x != minx && v[i].x != maxx) || (v[i].y != miny && v[i].y != maxy)) return 0;
        if (v[i].u != (v[i].x == minx ? minu : maxu) || v[i].v != (v[i].y == miny ? minv : maxv)) return 0;      /* no flips */
    }
    *x0 = minx; *y0 = miny; *w = ww; *h = hh; *u0 = minu; *v0 = minv; return 1;
}
/* FNV-1a over (tp, w, h, palette indexes of the w*h texels). uniform = 1 if all texels are equal. */
static uint64_t rect_hash(int tp, int tx, int ty, int u0, int v0, int w, int h, int *uniform)
{
    uint64_t hs = 1469598103934665603ull; int first = -1; *uniform = 1;
    #define MIX(b) do { hs ^= (uint8_t)(b); hs *= 1099511628211ull; } while (0)
    MIX(tp); MIX(w); MIX(h);
    for (int y = 0; y < h; y++) {
        const uint16_t *row = &S.vram[((ty + v0 + y) & 1023) << 10];
        for (int x = 0; x < w; x++) {
            int u = (u0 + x) & 255, idx;
            if (tp == 1) idx = (row[(tx + (u >> 1)) & 1023] >> ((u & 1) * 8)) & 255; else idx = (row[(tx + (u >> 2)) & 1023] >> ((u & 3) * 4)) & 15;
            if (first < 0) first = idx; else if (idx != first) *uniform = 0;
            MIX(idx);
        }
    }
    #undef MIX
    return hs ? hs : 1;
}
static okey_t *okey_find(uint64_t h)
{
    uint32_t sl = (uint32_t)(h >> 11) & (otab_n - 1);
    while (otab[sl].hash) { if (otab[sl].hash == h) return &otab[sl]; sl = (sl + 1) & (otab_n - 1); }
    return NULL;
}
static int overlay_add(const okey_t *k, int ox, int oy)
{
    for (int i = 0; i < npend; i++) if (pend[i].ovl == k->ovl && (pend[i].ox - ox >= -8 && pend[i].ox - ox <= 8 && pend[i].oy - oy >= -8 && pend[i].oy - oy <= 8)) return 1;   /* a sprite drawn twice one pixel apart is one placement */
    if (npend < 64) { pend_t *q = &pend[npend++]; q->ovl = k->ovl; q->ox = ox; q->oy = oy; q->x1 = S.da_x1; q->y1 = S.da_y1; q->x2 = S.da_x2; q->y2 = S.da_y2;
        int kw = k->meta ? (k->meta & 0xffff) : 32, kh = k->meta ? ((k->meta >> 16) & 0xffff) : 32; q->bx0 = ox + k->ex; q->by0 = oy + k->ey; q->bx1 = q->bx0 + kw - 1; q->by1 = q->by0 + kh - 1; }
    return 1;
}

/* Scene capture (debug/tooling): at frame gpu_quadcap_frame every textured 4/8-bit 1:1 quad is appended to gpu_quadcap_path as
 * {i32 x, y, w, h, tp, clutx, cluty, flags(bit0 = uniform); u64 hash; w*h*4 RGBA (decoded with the palette in effect)}, preceded by "G13C" + u32 1. */
const char *gpu_quadcap_path; uint64_t gpu_quadcap_frame; static FILE *quadcap_f;
static void quadcap_record(const vtx_t *v, int nv, int clutx, int cluty)
{
    int x0, y0, w, h, u0, v0;
    if (!gpu_quadcap_path || gpu_frame_no != gpu_quadcap_frame || S.tp > 1 || !quad_rect(v, nv, &x0, &y0, &w, &h, &u0, &v0)) return;
    if (!quadcap_f) { quadcap_f = fopen(gpu_quadcap_path, "wb"); if (!quadcap_f) return; fwrite("G13C", 1, 4, quadcap_f); uint32_t ver = 1; fwrite(&ver, 4, 1, quadcap_f); }
    int uni; uint64_t hs = rect_hash(S.tp, S.tx, S.ty, u0, v0, w, h, &uni);
    int32_t hd[8] = { x0, y0, w, h, S.tp, clutx, cluty, uni }; fwrite(hd, 4, 8, quadcap_f); fwrite(&hs, 8, 1, quadcap_f);
    uint16_t pal[256]; int n = S.tp == 0 ? 16 : 256; for (int i = 0; i < n; i++) pal[i] = S.vram[((cluty & 1023) << 10) | ((clutx + i) & 1023)];
    for (int y = 0; y < h; y++) {
        const uint16_t *row = &S.vram[((S.ty + v0 + y) & 1023) << 10];
        for (int x = 0; x < w; x++) {
            int u = (u0 + x) & 255, idx; uint16_t c;
            if (S.tp == 1) idx = (row[(S.tx + (u >> 1)) & 1023] >> ((u & 1) * 8)) & 255; else idx = (row[(S.tx + (u >> 2)) & 1023] >> ((u & 3) * 4)) & 15;
            c = pal[idx]; uint8_t px[4] = { (uint8_t)((c & 31) << 3 | (c & 31) >> 2), (uint8_t)((c >> 5 & 31) << 3 | (c >> 5 & 31) >> 2), (uint8_t)((c >> 10 & 31) << 3 | (c >> 10 & 31) >> 2), (uint8_t)(c == 0 ? 0 : 255) };
            fwrite(px, 1, 4, quadcap_f);
        }
    }
    fflush(quadcap_f);
}

void gpu_overlay_clear(void)
{
    for (int i = 0; i < novl; i++) free(ovls[i].rgba);
    free(ovls); free(otab); ovls = NULL; otab = NULL; novl = 0; otab_n = 0; npend = 0; nrect_keys = 0; memset(odims, 0, sizeof odims);
}

/* Returns 1 if the textured quad shows a tile of an overlay picture (then it must not be drawn). */
static int overlay_match(const vtx_t *v, int nv)
{
    if (!novl || S.tp > 1) return 0;
    if (nrect_keys) {                                       /* sprite rectangles (any 4/8-bit 1:1 quad whose size appears in a key) */
        int x0, y0, w, h, u0, v0;
        if (quad_rect(v, nv, &x0, &y0, &w, &h, &u0, &v0) && (odims[(h * 256 + w) >> 3] >> ((h * 256 + w) & 7) & 1)) {
            int uni; uint64_t hs = rect_hash(S.tp, S.tx, S.ty, u0, v0, w, h, &uni);
            const okey_t *k = uni ? NULL : okey_find(hs);
            if (k) { overlay_add(k, x0 - k->ex, y0 - k->ey); return !ovls[k->ovl].nosup; }
        }
    }
    if (S.tp != 0) return 0;
    int bi = 0; for (int i = 1; i < nv; i++) if (v[i].y < v[bi].y || (v[i].y == v[bi].y && v[i].x < v[bi].x)) bi = i;
    int u = v[bi].u, vv = v[bi].v, wx = S.tx + (u >> 2), wy = S.ty + vv;
    if ((u & 3) || wx + 8 > 1024 || wy + 32 > 1024) return 0;
    uint8_t tile[512];
    for (int r = 0; r < 32; r++) { const uint16_t *row = &S.vram[((wy + r) << 10) + wx]; for (int c = 0; c < 8; c++) { tile[r * 16 + c * 2] = (uint8_t)row[c]; tile[r * 16 + c * 2 + 1] = (uint8_t)(row[c] >> 8); } }
    uint64_t h = fnv64(tile, 512); if (!h) h = 1;
    const okey_t *k = okey_find(h);
    if (!k || k->meta) return 0;
    overlay_add(k, v[bi].x - k->ex, v[bi].y - k->ey); return !ovls[k->ovl].nosup;
}

static void overlay_flush(void)
{
    if (getenv("G13_OVDBG") && npend) { fprintf(stderr, "frame %llu pend:", (unsigned long long)gpu_frame_no); for (int i = 0; i < npend; i++) fprintf(stderr, " [%d @%d,%d %dx%d]", pend[i].ovl, pend[i].ox, pend[i].oy, ovls[pend[i].ovl].w, ovls[pend[i].ovl].h); fputc(10, stderr); }
    for (int i = 0; i < npend; i++) {
        const pend_t *q = &pend[i]; const ovl_t *o = &ovls[q->ovl];
        int fac[3] = { 256, 256, 256 };
        if (o->sample) {
            int vx = q->ox + o->sx, vy = q->oy + o->sy;
            if (vx >= 0 && vx < 1024 && vy >= 0 && vy < 1024) {
                uint16_t c = S.vram[(vy << 10) + vx]; int cur[3] = { (c & 31) * 255 / 31, ((c >> 5) & 31) * 255 / 31, ((c >> 10) & 31) * 255 / 31 };
                for (int k = 0; k < 3; k++) if (o->ref[k] >= 16) { int f = cur[k] * 256 / o->ref[k]; fac[k] = f > 256 ? 256 : f; }
            }
        }
        int cx1 = q->x1, cy1 = q->y1, cx2 = q->x2, cy2 = q->y2;
        for (int j = 0; j < npend; j++) if (pend[j].ovl == q->ovl && (pend[j].ox != q->ox || pend[j].oy != q->oy) && pend[j].y1 == q->y1 && pend[j].x1 == q->x1 && pend[j].y2 == q->y2 && pend[j].x2 == q->x2) {   /* same draw area only (not the same picture in the other buffer): the picture's pieces are drawn apart (slide-in): show each piece's own slice */
            if (q->bx0 > cx1) cx1 = q->bx0; if (q->by0 > cy1) cy1 = q->by0; if (q->bx1 < cx2) cx2 = q->bx1; if (q->by1 < cy2) cy2 = q->by1; break; }
        for (int y = 0; y < o->h; y++) {
            int vy = q->oy + y; if (vy < cy1 || vy > cy2 || vy < 0 || vy > 1023) continue;
            for (int x = 0; x < o->w; x++) {
                int vx = q->ox + x; if (vx < cx1 || vx > cx2 || vx < 0 || vx > 1023) continue;
                const uint8_t *px = &o->rgba[((size_t)y * o->w + x) * 4]; int a = px[3]; if (!a) continue;
                uint16_t *d = &S.vram[(vy << 10) + vx]; int dr = *d & 31, dg = (*d >> 5) & 31, db = (*d >> 10) & 31;
                int sr = (px[0] * fac[0]) >> 8, sg = (px[1] * fac[1]) >> 8, sb = (px[2] * fac[2]) >> 8;
                int r = ((sr >> 3) * a + dr * (255 - a)) / 255, g = ((sg >> 3) * a + dg * (255 - a)) / 255, b = ((sb >> 3) * a + db * (255 - a)) / 255;
                *d = (uint16_t)(r | (g << 5) | (b << 10));
            }
        }
    }
    npend = 0;
}

static void exec_poly(void)
{
    uint32_t cmd = S.fifo[0] >> 24;
    int quad = (cmd & 0x08) != 0, tex = (cmd & 0x04) != 0, gour = (cmd & 0x10) != 0, nv = quad ? 4 : 3;
    prim_t p = { tex, (cmd & 0x02) != 0, gour, (cmd & 0x01) != 0, 0, 0 };
    vtx_t v[4]; int idx = 1; uint32_t col = S.fifo[0];
    uint32_t clut = 0, tpage = 0;
    for (int i = 0; i < nv; i++) {
        if (gour && i > 0) col = S.fifo[idx++];
        uint32_t xy = S.fifo[idx++];
        v[i].x = sext((int)(xy & 0xffff), 11) + S.off_x; v[i].y = sext((int)(xy >> 16), 11) + S.off_y;
        v[i].r = col & 0xff; v[i].g = (col >> 8) & 0xff; v[i].b = (col >> 16) & 0xff; v[i].u = v[i].v = 0;
        if (tex) { uint32_t t = S.fifo[idx++]; v[i].u = t & 0xff; v[i].v = (t >> 8) & 0xff; if (i == 0) clut = t >> 16; if (i == 1) tpage = t >> 16; }
    }
    if (tex) { decode_tpage(tpage); p.clutx = (int)(clut & 0x3f) << 4; p.cluty = (int)(clut >> 6) & 0x3ff;
        if (gpu_quadcap_path) quadcap_record(v, nv, p.clutx, p.cluty);
        if (gpu_texdump_dir) { int a = v[0].u, b = v[0].v, c = v[0].u, d = v[0].v; for (int i = 1; i < nv; i++) { if (v[i].u < a) a = v[i].u; if (v[i].u > c) c = v[i].u; if (v[i].v < b) b = v[i].v; if (v[i].v > d) d = v[i].v; } tex_record(S.tp, S.tx, S.ty, p.clutx, p.cluty, a, b, c, d); } }
    if (gpu_log) {
        fprintf(stderr, "POLY#%llu cmd=%02x nv=%d tp=%d abr=%d tx=%d ty=%d clut=%d,%d win=%d,%d/%d,%d:", (unsigned long long)S.prims, (unsigned)cmd, nv, S.tp, S.abr, S.tx, S.ty, p.clutx, p.cluty, S.tw_mask_x, S.tw_mask_y, S.tw_off_x, S.tw_off_y);
        for (int i = 0; i < nv; i++) fprintf(stderr, " (%d,%d u%d v%d)", v[i].x, v[i].y, v[i].u, v[i].v);
        fprintf(stderr, "\n");
    }
    S.prims++;
    if (tex && novl && overlay_match(v, nv)) return;
    if (!cull(&v[0], &v[1]) && !cull(&v[1], &v[2]) && !cull(&v[2], &v[0])) tri(v[0], v[1], v[2], &p);
    if (quad && !cull(&v[1], &v[2]) && !cull(&v[2], &v[3]) && !cull(&v[3], &v[1])) tri(v[1], v[2], v[3], &p);
}

/* ------------------------------------------------------------------ lines */
static void draw_line(vtx_t a, vtx_t b, int gour, int semi)
{
    int dx = b.x - a.x, dy = b.y - a.y, n = imax(abs(dx), abs(dy));
    if (abs(dx) > 1023 || abs(dy) > 1023) return;
    for (int i = 0; i <= n; i++) {
        int x = n ? a.x + (dx * i + (dx >= 0 ? n / 2 : -n / 2)) / n : a.x, y = n ? a.y + (dy * i + (dy >= 0 ? n / 2 : -n / 2)) / n : a.y;
        if (x < S.da_x1 || x > S.da_x2 || y < S.da_y1 || y > S.da_y2) continue;
        int r = a.r, g = a.g, bl = a.b;
        if (gour && n) { r = a.r + (b.r - a.r) * i / n; g = a.g + (b.g - a.g) * i / n; bl = a.b + (b.b - a.b) * i / n; }
        put_pixel(x, y, r >> 3, g >> 3, bl >> 3, 0, semi);
    }
}
static vtx_t line_vertex(uint32_t xy, uint32_t col)
{
    vtx_t v; v.x = sext((int)(xy & 0xffff), 11) + S.off_x; v.y = sext((int)(xy >> 16), 11) + S.off_y;
    v.r = col & 0xff; v.g = (col >> 8) & 0xff; v.b = (col >> 16) & 0xff; v.u = v.v = 0; return v;
}
static void exec_line(void)
{
    uint32_t cmd = S.fifo[0] >> 24; int gour = (cmd & 0x10) != 0, semi = (cmd & 0x02) != 0;
    uint32_t c0 = S.fifo[0], c1 = gour ? S.fifo[2] : c0, xy0 = S.fifo[1], xy1 = gour ? S.fifo[3] : S.fifo[2];
    vtx_t a = line_vertex(xy0, c0), b = line_vertex(xy1, c1);
    S.prims++;
    draw_line(a, b, gour, semi);
    if (cmd & 0x08) { S.polyline = 1; S.pl_last = b; S.pl_cmd = S.fifo[0]; S.pl_gour = gour; }
}

/* ------------------------------------------------------------------ rectangles / sprites */
static void exec_rect(void)
{
    uint32_t cmd = S.fifo[0] >> 24; int tex = (cmd & 0x04) != 0, semi = (cmd & 0x02) != 0, raw = (cmd & 1) != 0;
    int size = (cmd >> 3) & 3, idx = 1;
    uint32_t col = S.fifo[0], xy = S.fifo[idx++], uvc = 0, wh;
    if (tex) uvc = S.fifo[idx++];
    int w, h;
    if (size == 0) { wh = S.fifo[idx++]; w = (int)(wh & 0x3ff); h = (int)((wh >> 16) & 0x3ff); }
    else { w = h = size == 1 ? 1 : size == 2 ? 8 : 16; }
    int x0 = sext((int)(xy & 0xffff), 11) + S.off_x, y0 = sext((int)(xy >> 16), 11) + S.off_y;
    int sr = col & 0xff, sg = (col >> 8) & 0xff, sb = (col >> 16) & 0xff;
    int clutx = (int)((uvc >> 16) & 0x3f) << 4, cluty = (int)((uvc >> 22) & 0x3ff), u0 = uvc & 0xff, v0 = (uvc >> 8) & 0xff;
    int du = S.flip_x ? -1 : 1, dv = S.flip_y ? -1 : 1;
    if (gpu_log) fprintf(stderr, "RECT#%llu cmd=%02x at %d,%d size %dx%d tp=%d tx=%d ty=%d uv=%d,%d clut=%d,%d flip=%d%d\n", (unsigned long long)S.prims, (unsigned)cmd, x0, y0, w, h, S.tp, S.tx, S.ty, u0, v0, clutx, cluty, S.flip_x, S.flip_y);
    S.prims++;
    if (tex && gpu_texdump_dir && w <= 256 && h <= 256) tex_record(S.tp, S.tx, S.ty, clutx, cluty, u0, v0, u0 + w - 1, v0 + h - 1);
    int xa = imax(x0, S.da_x1), xb = imin(x0 + w - 1, S.da_x2);
    if (xa > xb) return;
    ctx_t cx; ctx_setup(&cx, tex, semi, 0, raw, clutx, cluty);
    cx.ident = raw || (sr == 128 && sg == 128 && sb == 128);
    cx.umin = cx.vmin = -(1 << 20); cx.umax = cx.vmax = (1 << 20);   /* sprites wrap (u & 255) */
    for (int j = 0; j < h; j++) {
        int y = y0 + j; if (y < S.da_y1 || y > S.da_y2) continue;
        span_t s; memset(&s, 0, sizeof s);
        s.r = sr << FX; s.g = sg << FX; s.b = sb << FX;
        if (raw) { s.r = s.g = s.b = 128 << FX; }
        s.u = (u0 + (xa - x0) * du) << FX; s.du = du << FX; s.v = (v0 + j * dv) << FX;
        /* untextured raw colour: 'raw' only applies to textures */
        if (!tex) { s.r = sr << FX; s.g = sg << FX; s.b = sb << FX; }
        draw_span(y, xa, xb, s, &cx);
    }
}

/* ------------------------------------------------------------------ VRAM operations */
static void exec_fill(void)
{
    uint32_t col = S.fifo[0]; int x = (int)(S.fifo[1] & 0x3ff), y = (int)((S.fifo[1] >> 16) & 0x3ff);
    int w = (int)(S.fifo[2] & 0x3ff), h = (int)((S.fifo[2] >> 16) & 0x3ff);
    uint16_t c = (uint16_t)(((col & 0xff) >> 3) | ((((col >> 8) & 0xff) >> 3) << 5) | ((((col >> 16) & 0xff) >> 3) << 10));
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) S.vram[(((y + j) & 1023) << 10) | ((x + i) & 1023)] = c;
}
static void exec_copy(void)
{
    int sx = (int)(S.fifo[1] & 0x3ff), sy = (int)((S.fifo[1] >> 16) & 0x3ff), dx = (int)(S.fifo[2] & 0x3ff), dy = (int)((S.fifo[2] >> 16) & 0x3ff);
    int w = (int)(S.fifo[3] & 0x3ff), h = (int)((S.fifo[3] >> 16) & 0x3ff);
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
        uint16_t px = S.vram[(((sy + j) & 1023) << 10) | ((sx + i) & 1023)];
        uint16_t *d = &S.vram[(((dy + j) & 1023) << 10) | ((dx + i) & 1023)];
        if (S.check_stp && (*d & 0x8000)) continue;
        *d = S.draw_stp ? (uint16_t)(px | 0x8000) : px;
    }
}
static void xfer_pixel(uint16_t px)
{
    uint16_t *d = &S.vram[((S.xfer_cy & 1023) << 10) | (S.xfer_cx & 1023)];
    if (!(S.check_stp && (*d & 0x8000))) *d = S.draw_stp ? (uint16_t)(px | 0x8000) : px;
    if (++S.xfer_cx >= S.xfer_x + S.xfer_w) { S.xfer_cx = S.xfer_x; S.xfer_cy++; if (S.xfer_cy >= S.xfer_y + S.xfer_h) S.xfer = 0; }
}

/* ------------------------------------------------------------------ GP0 */
static int packet_len(uint32_t cmd)
{
    if (cmd == 0x02) return 3;
    if (cmd >= 0x20 && cmd <= 0x3f) {
        int nv = (cmd & 0x08) ? 4 : 3, tex = (cmd & 4) != 0, gour = (cmd & 0x10) != 0;
        return 1 + (gour ? nv - 1 : 0) + nv + (tex ? nv : 0);
    }
    if (cmd >= 0x40 && cmd <= 0x5f) return (cmd & 0x10) ? 4 : 3;
    if (cmd >= 0x60 && cmd <= 0x7f) return 2 + ((cmd & 4) ? 1 : 0) + ((((cmd >> 3) & 3) == 0) ? 1 : 0);
    if (cmd >= 0x80 && cmd <= 0x9f) return 4;
    if (cmd >= 0xa0 && cmd <= 0xdf) return 3;
    return 1;
}

static void exec_packet(void)
{
    uint32_t cmd = S.fifo[0] >> 24, d = S.fifo[0];
    if (gpu_nodraw && ((cmd >= 0x20 && cmd <= 0x7f))) return;   /* profiling: skip rasterisation */
    if (cmd == 0x02) exec_fill();
    else if (cmd >= 0x20 && cmd <= 0x3f) exec_poly();
    else if (cmd >= 0x40 && cmd <= 0x5f) exec_line();
    else if (cmd >= 0x60 && cmd <= 0x7f) exec_rect();
    else if (cmd >= 0x80 && cmd <= 0x9f) exec_copy();
    else if (cmd >= 0xa0 && cmd <= 0xbf) {
        S.xfer_x = (int)(S.fifo[1] & 0x3ff); S.xfer_y = (int)((S.fifo[1] >> 16) & 0x3ff);
        S.xfer_w = (int)(S.fifo[2] & 0x3ff); S.xfer_h = (int)((S.fifo[2] >> 16) & 0x3ff);
        if (!S.xfer_w) S.xfer_w = 0x400; if (!S.xfer_h) S.xfer_h = 0x400;
        S.xfer_cx = S.xfer_x; S.xfer_cy = S.xfer_y; S.xfer = 1;
        if (gpu_trace_loads) fprintf(stderr, "VLOAD f=%llu x=%d y=%d w=%d h=%d src=%08x\n", (unsigned long long)gpu_frame_no, S.xfer_x, S.xfer_y, S.xfer_w, S.xfer_h, gpu_src_addr);
    } else if (cmd >= 0xc0 && cmd <= 0xdf) {
        S.rd_x = (int)(S.fifo[1] & 0x3ff); S.rd_y = (int)((S.fifo[1] >> 16) & 0x3ff);
        S.rd_w = (int)(S.fifo[2] & 0x3ff); S.rd_h = (int)((S.fifo[2] >> 16) & 0x3ff);
        if (!S.rd_w) S.rd_w = 0x400; if (!S.rd_h) S.rd_h = 0x400;
        S.rd_cx = S.rd_x; S.rd_cy = S.rd_y; S.rd = 1; S.status |= 1u << 27;
    } else switch (cmd) {
    case 0xe1: decode_tpage(d & 0xffffff); break;
    case 0xe2: S.tw_mask_x = (int)(d & 0x1f) << 3; S.tw_mask_y = (int)((d >> 5) & 0x1f) << 3;
               S.tw_off_x = (int)((d >> 10) & 0x1f) << 3; S.tw_off_y = (int)((d >> 15) & 0x1f) << 3; break;
    case 0xe3: S.da_x1 = (int)(d & 1023); S.da_y1 = (int)((d >> 10) & 1023); break;
    case 0xe4: S.da_x2 = (int)(d & 1023); S.da_y2 = (int)((d >> 10) & 1023); break;
    case 0xe5: S.off_x = sext((int)(d & 2047), 11); S.off_y = sext((int)((d >> 11) & 2047), 11); break;
    case 0xe6: S.draw_stp = (int)(d & 1); S.check_stp = (int)((d >> 1) & 1); S.status = (S.status & ~(3u << 11)) | ((d & 3) << 11); break;
    default: break;
    }
}

void gpu_gp0_write(uint32_t w)
{
    S.words++;
    if (S.xfer) { xfer_pixel((uint16_t)w); if (S.xfer) xfer_pixel((uint16_t)(w >> 16)); else if (0) {} return; }
    if (S.polyline) {
        if ((w & 0xf000f000u) == 0x50005000u) { S.polyline = 0; return; }
        uint32_t col = S.pl_cmd; int gour = S.pl_gour; static uint32_t pending_col; static int have_col;
        if (gour && !have_col) { pending_col = w; have_col = 1; return; }
        if (gour) col = pending_col;
        have_col = 0;
        vtx_t b = line_vertex(w, col);
        S.prims++;
        draw_line(S.pl_last, b, gour, (S.pl_cmd >> 25) & 1);
        S.pl_last = b; return;
    }
    if (S.fifo_n == 0) { S.fifo_need = packet_len(w >> 24); }
    S.fifo[S.fifo_n++] = w;
    if (S.fifo_n >= S.fifo_need) { exec_packet(); S.fifo_n = 0; S.fifo_need = 0; }
}

uint32_t gpu_read(void)
{
    if (S.rd) {
        uint16_t a = S.vram[((S.rd_cy & 1023) << 10) | (S.rd_cx & 1023)];
        if (++S.rd_cx >= S.rd_x + S.rd_w) { S.rd_cx = S.rd_x; S.rd_cy++; if (S.rd_cy >= S.rd_y + S.rd_h) { S.rd = 0; S.status &= ~(1u << 27); return a; } }
        uint16_t b = S.vram[((S.rd_cy & 1023) << 10) | (S.rd_cx & 1023)];
        if (++S.rd_cx >= S.rd_x + S.rd_w) { S.rd_cx = S.rd_x; S.rd_cy++; if (S.rd_cy >= S.rd_y + S.rd_h) { S.rd = 0; S.status &= ~(1u << 27); } }
        return (uint32_t)a | ((uint32_t)b << 16);
    }
    return S.info;
}

/* ------------------------------------------------------------------ GP1 */
void gpu_gp1_write(uint32_t d)
{
    switch (d >> 24) {
    case 0x00: gpu_reset(); break;
    case 0x01: S.fifo_n = S.fifo_need = 0; S.polyline = 0; S.xfer = 0; break;
    case 0x03: S.status = (S.status & ~(1u << 23)) | ((d & 1) << 23); break;
    case 0x04:
        S.status &= ~(3u << 29); S.status |= (d & 3) << 29; S.status &= ~(1u << 25);
        if ((d & 3) == 1 || (d & 3) == 2) S.status |= 1u << 25;
        break;
    case 0x05: S.disp_x = (int)(d & 1023); S.disp_y = (int)((d >> 10) & 1023); break;
    case 0x06: S.hstart = (int)(d & 4095); S.hend = (int)((d >> 12) & 4095); break;
    case 0x07: S.vstart = (int)(d & 1023); S.vend = (int)((d >> 10) & 2047); break;
    case 0x08:
        S.status &= ~(127u << 16); S.status |= (d & 0x3f) << 17; S.status |= ((d & 0x40) >> 6) << 16;
        break;
    case 0x10:
        switch (d & 0xff) {
        case 3: S.info = (uint32_t)(S.da_x1 | (S.da_y1 << 10)); break;
        case 4: S.info = (uint32_t)(S.da_x2 | (S.da_y2 << 10)); break;
        case 5: S.info = (uint32_t)((S.off_x & 2047) | ((S.off_y & 2047) << 11)); break;
        case 7: S.info = 2; break;
        default: break;
        }
        break;
    default: break;
    }
}

uint32_t gpu_status(uint64_t frame, uint32_t vpos)
{
    uint32_t d = S.status; (void)frame;
    if (((d & (1u << 22)) && (d & (1u << 13))) || (!(d & (1u << 22)) && (vpos & 1))) d |= 1u << 31;
    return d;
}
void gpu_vblank(void)
{
    gpu_frame_no++;
    if (npend) overlay_flush();
    if (S.status & (1u << 22)) S.status ^= 1u << 13; else S.status |= 1u << 13;
}

/* ------------------------------------------------------------------ display */
void gpu_get_display(uint8_t *buf, int *w, int *h)
{
    int hres = (S.status & (1u << 16)) ? 368 : 256 + 64 * (int)((S.status >> 17) & 3);
    if (!(S.status & (1u << 16))) { static const int tb[4] = { 256, 320, 512, 640 }; hres = tb[(S.status >> 17) & 3]; }
    int vres = (S.status & (1u << 19)) ? 480 : 240;
    int div = hres == 256 ? 10 : hres == 320 ? 8 : hres == 512 ? 5 : hres == 640 ? 4 : 7;
    int is24 = (S.status >> 21) & 1;
    *w = hres; *h = vres;
    memset(buf, 0, (size_t)hres * vres * 4);
    for (int i = 0; i < hres * vres; i++) buf[i * 4 + 3] = 255;
    /* visible window from horizontal / vertical ranges */
    int px0 = (S.hstart - 0x260) / div; if (px0 < 0) px0 = 0;
    int px1 = (S.hend - 0x260) / div; if (px1 > hres) px1 = hres;
    int lines = (vres == 480) ? 2 : 1;
    int py0 = (S.vstart - 0x10) * lines; if (py0 < 0) py0 = 0;
    int py1 = (S.vend - 0x10) * lines; if (py1 > vres) py1 = vres;
    if (S.hend <= S.hstart || S.vend <= S.vstart) { px0 = 0; px1 = hres; py0 = 0; py1 = vres; }
    if (gpu_log) fprintf(stderr, "[gpu] display %dx%d disp=%d,%d visible x %d-%d y %d-%d hrange %x-%x vrange %x-%x status=%08x\n", hres, vres, S.disp_x, S.disp_y, px0, px1, py0, py1, S.hstart, S.hend, S.vstart, S.vend, S.status);
    for (int y = py0; y < py1; y++) {
        for (int x = px0; x < px1; x++) {
            int sx = x - px0, sy = y - py0;
            uint8_t *o = &buf[((size_t)y * hres + x) * 4];
            if (!is24) {
                uint16_t c = S.vram[(((S.disp_y + sy) & 1023) << 10) | ((S.disp_x + sx) & 1023)];
                o[0] = (uint8_t)(((c & 31) << 3) | ((c & 31) >> 2)); o[1] = (uint8_t)((((c >> 5) & 31) << 3) | (((c >> 5) & 31) >> 2));
                o[2] = (uint8_t)((((c >> 10) & 31) << 3) | (((c >> 10) & 31) >> 2));
            } else {
                int bx = (S.disp_x * 2 + sx * 3), row = (S.disp_y + sy) & 1023;
                const uint8_t *r = (const uint8_t *)&S.vram[row << 10];
                o[0] = r[(bx) & 2047]; o[1] = r[(bx + 1) & 2047]; o[2] = r[(bx + 2) & 2047];
            }
        }
    }
}
