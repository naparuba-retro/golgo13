/* Minimal RFC 1951 (deflate) decoder, canonical-Huffman bit-by-bit (same structure as zlib's puff.c). Used only to read the
 * user's ROM zip once at import time. Returns 0 on success, -1 on corrupt data or output overflow. */
#include <string.h>
#include "inflate.h"

typedef struct { const uint8_t *src; size_t n, pos; uint32_t buf; int cnt; uint8_t *dst; size_t cap, len; int err; } st_t;
typedef struct { uint16_t count[16]; uint16_t symbol[288]; } huff_t;

static int getbit(st_t *s)
{
    if (!s->cnt) { if (s->pos >= s->n) { s->err = 1; return 0; } s->buf = s->src[s->pos++]; s->cnt = 8; }
    int b = s->buf & 1; s->buf >>= 1; s->cnt--; return b;
}
static uint32_t getbits(st_t *s, int n) { uint32_t v = 0; for (int i = 0; i < n; i++) v |= (uint32_t)getbit(s) << i; return v; }

static void build(huff_t *h, const uint8_t *len, int n)
{
    uint16_t offs[16];
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    offs[1] = 0; for (int i = 1; i < 15; i++) offs[i + 1] = (uint16_t)(offs[i] + h->count[i]);
    for (int i = 0; i < n; i++) if (len[i]) h->symbol[offs[len[i]]++] = (uint16_t)i;
}
static int decode(st_t *s, const huff_t *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= getbit(s);
        int c = h->count[len];
        if (code - c < first) return h->symbol[index + (code - first)];
        index += c; first += c; first <<= 1; code <<= 1;
        if (s->err) return -1;
    }
    return -1;
}

static const uint16_t LB[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const uint16_t LE[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const uint16_t DB[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const uint16_t DE[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

static int codes(st_t *s, const huff_t *lc, const huff_t *dc)
{
    for (;;) {
        int sym = decode(s, lc);
        if (sym < 0 || s->err) return -1;
        if (sym < 256) { if (s->len >= s->cap) return -1; s->dst[s->len++] = (uint8_t)sym; }
        else if (sym == 256) return 0;
        else {
            sym -= 257; if (sym >= 29) return -1;
            size_t len = LB[sym] + getbits(s, LE[sym]);
            int ds = decode(s, dc); if (ds < 0 || ds >= 30) return -1;
            size_t dist = DB[ds] + getbits(s, DE[ds]);
            if (s->err || dist > s->len || s->len + len > s->cap) return -1;
            for (size_t i = 0; i < len; i++) { s->dst[s->len] = s->dst[s->len - dist]; s->len++; }
        }
    }
}

int rt_inflate(const uint8_t *src, size_t n, uint8_t *dst, size_t cap, size_t *outlen)
{
    st_t s; memset(&s, 0, sizeof s); s.src = src; s.n = n; s.dst = dst; s.cap = cap;
    int last;
    do {
        last = getbit(&s); int type = (int)getbits(&s, 2);
        if (s.err) return -1;
        if (type == 0) {
            s.cnt = 0;
            if (s.pos + 4 > s.n) return -1;
            unsigned len = src[s.pos] | (src[s.pos + 1] << 8), nlen = src[s.pos + 2] | (src[s.pos + 3] << 8); s.pos += 4;
            if (len != (~nlen & 0xffff) || s.pos + len > s.n || s.len + len > s.cap) return -1;
            memcpy(dst + s.len, src + s.pos, len); s.pos += len; s.len += len;
        } else if (type == 1) {
            static huff_t lc, dc; static int init;
            if (!init) { uint8_t l[288]; int i; for (i = 0; i < 144; i++) l[i] = 8; for (; i < 256; i++) l[i] = 9; for (; i < 280; i++) l[i] = 7; for (; i < 288; i++) l[i] = 8;
                         build(&lc, l, 288); for (i = 0; i < 30; i++) l[i] = 5; build(&dc, l, 30); init = 1; }
            if (codes(&s, &lc, &dc)) return -1;
        } else if (type == 2) {
            static const uint8_t order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
            uint8_t lens[320]; huff_t lc, dc;
            int nlen = (int)getbits(&s, 5) + 257, ndist = (int)getbits(&s, 5) + 1, ncode = (int)getbits(&s, 4) + 4;
            if (nlen > 286 || ndist > 30) return -1;
            int i; for (i = 0; i < 19; i++) lens[i] = 0;
            for (i = 0; i < ncode; i++) lens[order[i]] = (uint8_t)getbits(&s, 3);
            build(&lc, lens, 19);
            uint8_t all[320]; i = 0;
            while (i < nlen + ndist) {
                int sym = decode(&s, &lc); if (sym < 0 || s.err) return -1;
                if (sym < 16) all[i++] = (uint8_t)sym;
                else {
                    int rep, val = 0;
                    if (sym == 16) { if (i == 0) return -1; val = all[i - 1]; rep = 3 + (int)getbits(&s, 2); }
                    else if (sym == 17) rep = 3 + (int)getbits(&s, 3); else rep = 11 + (int)getbits(&s, 7);
                    if (i + rep > nlen + ndist) return -1;
                    while (rep--) all[i++] = (uint8_t)val;
                }
            }
            build(&lc, all, nlen); build(&dc, all + nlen, ndist);
            if (codes(&s, &lc, &dc)) return -1;
        } else return -1;
    } while (!last);
    *outlen = s.len; return 0;
}
