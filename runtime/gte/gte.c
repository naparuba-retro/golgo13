/* PlayStation GTE (COP2) - C11 port.
 *
 * Behaviour (register layout, saturation, FLAG bits, 44-bit MAC overflow tracking, UNR divide)
 * follows MAME's src/devices/cpu/psx/gte.cpp:
 *   license:BSD-3-Clause, copyright-holders:smf, Copyright 2003-2013 smf
 * Re-expressed in C; the structure of every expression is kept so rounding/flags stay identical.
 * Verified against the original by tests/gte_test (random differential test).
 */
#include <stdint.h>
#include <string.h>
#include "gte_port.h"

/* little-endian host assumed (x86-64 / aarch64) */
typedef union {
    uint32_t d; int32_t sd;
    struct { uint16_t l, h; } w;
    struct { int16_t l, h; } sw;
    struct { uint8_t l, h, h2, h3; } b;
} pair_t;

static struct {
    pair_t cr[32];
    pair_t dr[32];
    int     sf;
    int64_t mac0, mac1, mac2, mac3;
} G_;
#define m_cp2cr G_.cr
#define m_cp2dr G_.dr
#define m_sf   G_.sf
#define m_mac0 G_.mac0
#define m_mac1 G_.mac1
#define m_mac2 G_.mac2
#define m_mac3 G_.mac3

#define GTE_SF(op)    (((op) >> 19) & 1)
#define GTE_MX(op)    (((op) >> 17) & 3)
#define GTE_V(op)     (((op) >> 15) & 3)
#define GTE_CV(op)    (((op) >> 13) & 3)
#define GTE_LM(op)    (((op) >> 10) & 1)
#define GTE_FUNCT(op) ((op) & 63)

#define VX0  (m_cp2dr[0].sw.l)
#define VY0  (m_cp2dr[0].sw.h)
#define VZ0  (m_cp2dr[1].sw.l)
#define R    (m_cp2dr[6].b.l)
#define Gc   (m_cp2dr[6].b.h)
#define Bc   (m_cp2dr[6].b.h2)
#define CODE (m_cp2dr[6].b.h3)
#define OTZ  (m_cp2dr[7].w.l)
#define IR0  (m_cp2dr[8].sw.l)
#define IR1  (m_cp2dr[9].sw.l)
#define IR2  (m_cp2dr[10].sw.l)
#define IR3  (m_cp2dr[11].sw.l)
#define SXY0 (m_cp2dr[12].d)
#define SX0  (m_cp2dr[12].sw.l)
#define SY0  (m_cp2dr[12].sw.h)
#define SXY1 (m_cp2dr[13].d)
#define SX1  (m_cp2dr[13].sw.l)
#define SY1  (m_cp2dr[13].sw.h)
#define SXY2 (m_cp2dr[14].d)
#define SX2  (m_cp2dr[14].sw.l)
#define SY2  (m_cp2dr[14].sw.h)
#define SZ0  (m_cp2dr[16].w.l)
#define SZ1  (m_cp2dr[17].w.l)
#define SZ2  (m_cp2dr[18].w.l)
#define SZ3  (m_cp2dr[19].w.l)
#define RGB0 (m_cp2dr[20].d)
#define R0   (m_cp2dr[20].b.l)
#define G0   (m_cp2dr[20].b.h)
#define B0   (m_cp2dr[20].b.h2)
#define RGB1 (m_cp2dr[21].d)
#define RGB2 (m_cp2dr[22].d)
#define R2   (m_cp2dr[22].b.l)
#define G2   (m_cp2dr[22].b.h)
#define B2   (m_cp2dr[22].b.h2)
#define CD2  (m_cp2dr[22].b.h3)
#define MAC0 (m_cp2dr[24].sd)
#define MAC1 (m_cp2dr[25].sd)
#define MAC2 (m_cp2dr[26].sd)
#define MAC3 (m_cp2dr[27].sd)
#define LZCR (m_cp2dr[31].d)

#define R11 (m_cp2cr[0].sw.l)
#define R12 (m_cp2cr[0].sw.h)
#define R13 (m_cp2cr[1].sw.l)
#define R21 (m_cp2cr[1].sw.h)
#define R22 (m_cp2cr[2].sw.l)
#define R23 (m_cp2cr[2].sw.h)
#define R31 (m_cp2cr[3].sw.l)
#define R32 (m_cp2cr[3].sw.h)
#define R33 (m_cp2cr[4].sw.l)
#define TRX (m_cp2cr[5].sd)
#define TRY (m_cp2cr[6].sd)
#define TRZ (m_cp2cr[7].sd)
#define L11 (m_cp2cr[8].sw.l)
#define L12 (m_cp2cr[8].sw.h)
#define L13 (m_cp2cr[9].sw.l)
#define L21 (m_cp2cr[9].sw.h)
#define L22 (m_cp2cr[10].sw.l)
#define L23 (m_cp2cr[10].sw.h)
#define L31 (m_cp2cr[11].sw.l)
#define L32 (m_cp2cr[11].sw.h)
#define L33 (m_cp2cr[12].sw.l)
#define RBK (m_cp2cr[13].sd)
#define GBK (m_cp2cr[14].sd)
#define BBK (m_cp2cr[15].sd)
#define LR1 (m_cp2cr[16].sw.l)
#define LR2 (m_cp2cr[16].sw.h)
#define LR3 (m_cp2cr[17].sw.l)
#define LG1 (m_cp2cr[17].sw.h)
#define LG2 (m_cp2cr[18].sw.l)
#define LG3 (m_cp2cr[18].sw.h)
#define LB1 (m_cp2cr[19].sw.l)
#define LB2 (m_cp2cr[19].sw.h)
#define LB3 (m_cp2cr[20].sw.l)
#define RFC (m_cp2cr[21].sd)
#define GFC (m_cp2cr[22].sd)
#define BFC (m_cp2cr[23].sd)
#define OFX (m_cp2cr[24].sd)
#define OFY (m_cp2cr[25].sd)
#define H   (m_cp2cr[26].sw.l)
#define DQA (m_cp2cr[27].sw.l)
#define DQB (m_cp2cr[28].sd)
#define ZSF3 (m_cp2cr[29].sw.l)
#define ZSF4 (m_cp2cr[30].sw.l)
#define FLAG (m_cp2cr[31].d)

#define VX(n) ((n) < 3 ? m_cp2dr[(n) << 1].sw.l : IR1)
#define VY(n) ((n) < 3 ? m_cp2dr[(n) << 1].sw.h : IR2)
#define VZ(n) ((n) < 3 ? m_cp2dr[((n) << 1) + 1].sw.l : IR3)
#define MX11(n) ((n) < 3 ? m_cp2cr[((n) << 3)].sw.l : -R << 4)
#define MX12(n) ((n) < 3 ? m_cp2cr[((n) << 3)].sw.h : R << 4)
#define MX13(n) ((n) < 3 ? m_cp2cr[((n) << 3) + 1].sw.l : IR0)
#define MX21(n) ((n) < 3 ? m_cp2cr[((n) << 3) + 1].sw.h : R13)
#define MX22(n) ((n) < 3 ? m_cp2cr[((n) << 3) + 2].sw.l : R13)
#define MX23(n) ((n) < 3 ? m_cp2cr[((n) << 3) + 2].sw.h : R13)
#define MX31(n) ((n) < 3 ? m_cp2cr[((n) << 3) + 3].sw.l : R22)
#define MX32(n) ((n) < 3 ? m_cp2cr[((n) << 3) + 3].sw.h : R22)
#define MX33(n) ((n) < 3 ? m_cp2cr[((n) << 3) + 4].sw.l : R22)
#define CV1(n) ((n) < 3 ? m_cp2cr[((n) << 3) + 5].sd : 0)
#define CV2(n) ((n) < 3 ? m_cp2cr[((n) << 3) + 6].sd : 0)
#define CV3(n) ((n) < 3 ? m_cp2cr[((n) << 3) + 7].sd : 0)

/* ---- 44-bit accumulator with overflow tracking (MAME int44) ---- */
typedef struct { int64_t v; int pos, neg; } i44;

static int64_t sext44(int64_t x)
{
    uint64_t u = (uint64_t)x & 0xfffffffffffull;
    if (u & 0x80000000000ull) u |= 0xfffff00000000000ull;
    return (int64_t)u;
}
static i44 i44_from(int64_t value)
{
    i44 r; r.v = value; r.pos = value > 0x7ffffffffffll; r.neg = value < -0x80000000000ll; return r;
}
static i44 i44_add(i44 a, int64_t add)
{
    i44 r; int64_t value = sext44(a.v + add);
    r.v = value;
    r.pos = a.pos || (value < 0 && a.v >= 0 && add >= 0);
    r.neg = a.neg || (value >= 0 && a.v < 0 && add < 0);
    return r;
}

static inline int clz32(uint32_t x) { int n = 0; if (!x) return 32; while (!(x & 0x80000000u)) { x <<= 1; n++; } return n; }
static inline int clz16(uint16_t x) { int n = 0; if (!x) return 16; while (!(x & 0x8000u)) { x = (uint16_t)(x << 1); n++; } return n; }

static int32_t LIM(int32_t value, int32_t max, int32_t min, uint32_t flag)
{
    if (value > max) { FLAG |= flag; return max; }
    else if (value < min) { FLAG |= flag; return min; }
    return value;
}

static inline int64_t gte_shift(int64_t a, int sf)
{
    if (sf > 0) return a >> 12;
    else if (sf < 0) return a << 12;
    return a;
}

static int32_t BOUNDS(i44 value, uint32_t max_flag, uint32_t min_flag)
{
    if (value.pos) FLAG |= max_flag;
    if (value.neg) FLAG |= min_flag;
    return (int32_t)gte_shift(value.v, m_sf);
}

static uint32_t gte_divide(uint16_t numerator, uint16_t denominator)
{
    if (numerator < (uint32_t)(denominator * 2)) {
        static const uint8_t table[] = {
            0xff, 0xfd, 0xfb, 0xf9, 0xf7, 0xf5, 0xf3, 0xf1, 0xef, 0xee, 0xec, 0xea, 0xe8, 0xe6, 0xe4, 0xe3,
            0xe1, 0xdf, 0xdd, 0xdc, 0xda, 0xd8, 0xd6, 0xd5, 0xd3, 0xd1, 0xd0, 0xce, 0xcd, 0xcb, 0xc9, 0xc8,
            0xc6, 0xc5, 0xc3, 0xc1, 0xc0, 0xbe, 0xbd, 0xbb, 0xba, 0xb8, 0xb7, 0xb5, 0xb4, 0xb2, 0xb1, 0xb0,
            0xae, 0xad, 0xab, 0xaa, 0xa9, 0xa7, 0xa6, 0xa4, 0xa3, 0xa2, 0xa0, 0x9f, 0x9e, 0x9c, 0x9b, 0x9a,
            0x99, 0x97, 0x96, 0x95, 0x94, 0x92, 0x91, 0x90, 0x8f, 0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x87, 0x86,
            0x85, 0x84, 0x83, 0x82, 0x81, 0x7f, 0x7e, 0x7d, 0x7c, 0x7b, 0x7a, 0x79, 0x78, 0x77, 0x75, 0x74,
            0x73, 0x72, 0x71, 0x70, 0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x69, 0x68, 0x67, 0x66, 0x65, 0x64,
            0x63, 0x62, 0x61, 0x60, 0x5f, 0x5e, 0x5d, 0x5d, 0x5c, 0x5b, 0x5a, 0x59, 0x58, 0x57, 0x56, 0x55,
            0x54, 0x53, 0x53, 0x52, 0x51, 0x50, 0x4f, 0x4e, 0x4d, 0x4d, 0x4c, 0x4b, 0x4a, 0x49, 0x48, 0x48,
            0x47, 0x46, 0x45, 0x44, 0x43, 0x43, 0x42, 0x41, 0x40, 0x3f, 0x3f, 0x3e, 0x3d, 0x3c, 0x3c, 0x3b,
            0x3a, 0x39, 0x39, 0x38, 0x37, 0x36, 0x36, 0x35, 0x34, 0x33, 0x33, 0x32, 0x31, 0x31, 0x30, 0x2f,
            0x2e, 0x2e, 0x2d, 0x2c, 0x2c, 0x2b, 0x2a, 0x2a, 0x29, 0x28, 0x28, 0x27, 0x26, 0x26, 0x25, 0x24,
            0x24, 0x23, 0x22, 0x22, 0x21, 0x20, 0x20, 0x1f, 0x1e, 0x1e, 0x1d, 0x1d, 0x1c, 0x1b, 0x1b, 0x1a,
            0x19, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14, 0x14, 0x13, 0x12, 0x12, 0x11, 0x11,
            0x10, 0x0f, 0x0f, 0x0e, 0x0e, 0x0d, 0x0d, 0x0c, 0x0c, 0x0b, 0x0a, 0x0a, 0x09, 0x09, 0x08, 0x08,
            0x07, 0x07, 0x06, 0x06, 0x05, 0x05, 0x04, 0x04, 0x03, 0x03, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00,
            0x00
        };
        int shift = clz16(denominator);
        int r1 = (int)((uint32_t)denominator << shift) & 0x7fff;
        int r2 = table[((r1 + 0x40) >> 7)] + 0x101;
        int r3 = ((0x80 - (r2 * (r1 + 0x8000))) >> 8) & 0x1ffff;
        uint32_t reciprocal = ((uint32_t)(r2 * r3) + 0x80) >> 8;
        return (uint32_t)(((uint64_t)reciprocal * ((uint32_t)numerator << shift) + 0x8000) >> 16);
    }
    return 0xffffffffu;
}

/* Setting bits 12 & 19-22 in FLAG does not set bit 31 */
static int32_t A1(i44 a) { m_mac1 = a.v; return BOUNDS(a, (1u << 31) | (1u << 30), (1u << 31) | (1u << 27)); }
static int32_t A2(i44 a) { m_mac2 = a.v; return BOUNDS(a, (1u << 31) | (1u << 29), (1u << 31) | (1u << 26)); }
static int32_t A3(i44 a) { m_mac3 = a.v; return BOUNDS(a, (1u << 31) | (1u << 28), (1u << 31) | (1u << 25)); }
#define A1i(x) A1(i44_from((int64_t)(x)))
#define A2i(x) A2(i44_from((int64_t)(x)))
#define A3i(x) A3(i44_from((int64_t)(x)))
static int32_t Lm_B1(int32_t a, int lm) { return LIM(a, 0x7fff, -0x8000 * !lm, (1u << 31) | (1u << 24)); }
static int32_t Lm_B2(int32_t a, int lm) { return LIM(a, 0x7fff, -0x8000 * !lm, (1u << 31) | (1u << 23)); }
static int32_t Lm_B3(int32_t a, int lm) { return LIM(a, 0x7fff, -0x8000 * !lm, (1u << 22)); }

static int32_t Lm_B3_sf(int64_t value, int sf, int lm)
{
    int32_t value_sf = (int32_t)gte_shift(value, sf);
    int32_t value_12 = (int32_t)gte_shift(value, 1);
    int max = 0x7fff, min = 0;
    if (lm == 0) min = -0x8000;
    if (value_12 < -0x8000 || value_12 > 0x7fff) FLAG |= (1u << 22);
    if (value_sf > max) return max;
    else if (value_sf < min) return min;
    return value_sf;
}

static int32_t Lm_C1(int32_t a) { return LIM(a, 0x00ff, 0x0000, (1u << 21)); }
static int32_t Lm_C2(int32_t a) { return LIM(a, 0x00ff, 0x0000, (1u << 20)); }
static int32_t Lm_C3(int32_t a) { return LIM(a, 0x00ff, 0x0000, (1u << 19)); }
static int32_t Lm_D(int64_t a, int sf) { return LIM((int32_t)gte_shift(a, sf), 0xffff, 0x0000, (1u << 31) | (1u << 18)); }

static uint32_t Lm_E(uint32_t result)
{
    if (result == 0xffffffffu) { FLAG |= (1u << 31) | (1u << 17); return 0x1ffff; }
    if (result > 0x1ffff) return 0x1ffff;
    return result;
}

static int64_t F(int64_t a)
{
    m_mac0 = a;
    if (a > 0x7fffffffll) FLAG |= (1u << 31) | (1u << 16);
    if (a < (int64_t)(int32_t)-0x80000000ll) FLAG |= (1u << 31) | (1u << 15);
    return a;
}

static int32_t Lm_G1(int64_t a)
{
    if (a > 0x3ff) { FLAG |= (1u << 31) | (1u << 14); return 0x3ff; }
    if (a < -0x400) { FLAG |= (1u << 31) | (1u << 14); return -0x400; }
    return (int32_t)a;
}
static int32_t Lm_G2(int64_t a)
{
    if (a > 0x3ff) { FLAG |= (1u << 31) | (1u << 13); return 0x3ff; }
    if (a < -0x400) { FLAG |= (1u << 31) | (1u << 13); return -0x400; }
    return (int32_t)a;
}

static int32_t Lm_H(int64_t value, int sf)
{
    int64_t value_sf = gte_shift(value, sf);
    int32_t value_12 = (int32_t)gte_shift(value, 1);
    int max = 0x1000, min = 0x0000;
    if (value_sf < min || value_sf > max) FLAG |= (1u << 12);
    if (value_12 > max) return max;
    if (value_12 < min) return min;
    return value_12;
}

/* ---- register access ---- */
uint32_t gte_getdr(int reg)
{
    switch (reg) {
    case 1: case 3: case 5: case 8: case 9: case 10: case 11:
        m_cp2dr[reg].d = (uint32_t)(int32_t)m_cp2dr[reg].sw.l; break;
    case 7: case 16: case 17: case 18: case 19:
        m_cp2dr[reg].d = (uint32_t)m_cp2dr[reg].w.l; break;
    case 15:
        m_cp2dr[reg].d = SXY2; break;
    case 28: case 29:
        m_cp2dr[reg].d = (uint32_t)(LIM(IR1 >> 7, 0x1f, 0, 0) | (LIM(IR2 >> 7, 0x1f, 0, 0) << 5) | (LIM(IR3 >> 7, 0x1f, 0, 0) << 10));
        break;
    }
    return m_cp2dr[reg].d;
}

void gte_setdr(int reg, uint32_t value)
{
    switch (reg) {
    case 15: SXY0 = SXY1; SXY1 = SXY2; SXY2 = value; break;
    case 28:
        IR1 = (int16_t)((value & 0x1f) << 7);
        IR2 = (int16_t)((value & 0x3e0) << 2);
        IR3 = (int16_t)((value & 0x7c00) >> 3);
        break;
    case 30:
        LZCR = (value & 0x80000000u) == 0 ? (uint32_t)clz32(value) : (uint32_t)clz32(~value);
        break;
    case 31: return;
    }
    m_cp2dr[reg].d = value;
}

uint32_t gte_getcr(int reg) { return m_cp2cr[reg].d; }

void gte_setcr(int reg, uint32_t value)
{
    switch (reg) {
    case 4: case 12: case 20: case 26: case 27: case 29: case 30:
        value = (uint32_t)(int32_t)(int16_t)value; break;
    case 31:
        value = value & 0x7ffff000u;
        if ((value & 0x7f87e000u) != 0) value |= 0x80000000u;
        break;
    }
    m_cp2cr[reg].d = value;
}

void gte_reset(void) { memset(&G_, 0, sizeof G_); }
const uint32_t *gte_dr_raw(void) { static uint32_t t[32]; for (int i = 0; i < 32; i++) t[i] = m_cp2dr[i].d; return t; }
const uint32_t *gte_cr_raw(void) { static uint32_t t[32]; for (int i = 0; i < 32; i++) t[i] = m_cp2cr[i].d; return t; }

int gte_docop2(uint32_t gteop)
{
    int v, lm, cv, mx;
    int32_t h_over_sz3 = 0;
    lm = GTE_LM(gteop);
    m_sf = GTE_SF(gteop);
    FLAG = 0;
    (void)Gc; (void)Bc;
#define COLOR_OUT() do { RGB0 = RGB1; RGB1 = RGB2; CD2 = CODE; \
    R2 = (uint8_t)Lm_C1(MAC1 >> 4); G2 = (uint8_t)Lm_C2(MAC2 >> 4); B2 = (uint8_t)Lm_C3(MAC3 >> 4); } while (0)
#define IR_OUT() do { IR1 = (int16_t)Lm_B1(MAC1, lm); IR2 = (int16_t)Lm_B2(MAC2, lm); IR3 = (int16_t)Lm_B3(MAC3, lm); } while (0)
#define LIGHT(vx, vy, vz) do { \
    MAC1 = A1i((int64_t)(L11 * (vx)) + (L12 * (vy)) + (L13 * (vz))); \
    MAC2 = A2i((int64_t)(L21 * (vx)) + (L22 * (vy)) + (L23 * (vz))); \
    MAC3 = A3i((int64_t)(L31 * (vx)) + (L32 * (vy)) + (L33 * (vz))); } while (0)
#define BKCOLOR() do { \
    MAC1 = A1(i44_add(i44_add(i44_add(i44_from((int64_t)RBK << 12), LR1 * IR1), LR2 * IR2), LR3 * IR3)); \
    MAC2 = A2(i44_add(i44_add(i44_add(i44_from((int64_t)GBK << 12), LG1 * IR1), LG2 * IR2), LG3 * IR3)); \
    MAC3 = A3(i44_add(i44_add(i44_add(i44_from((int64_t)BBK << 12), LB1 * IR1), LB2 * IR2), LB3 * IR3)); } while (0)
#define FARCOL() do { \
    MAC1 = A1i(((R << 4) * IR1) + (IR0 * Lm_B1(A1i(((int64_t)RFC << 12) - ((R << 4) * IR1)), 0))); \
    MAC2 = A2i(((Gc << 4) * IR2) + (IR0 * Lm_B2(A2i(((int64_t)GFC << 12) - ((Gc << 4) * IR2)), 0))); \
    MAC3 = A3i(((Bc << 4) * IR3) + (IR0 * Lm_B3(A3i(((int64_t)BFC << 12) - ((Bc << 4) * IR3)), 0))); } while (0)
#define MULCOL() do { \
    MAC1 = A1i((R << 4) * IR1); MAC2 = A2i((Gc << 4) * IR2); MAC3 = A3i((Bc << 4) * IR3); } while (0)

    switch (GTE_FUNCT(gteop)) {
    case 0x00: /* fall through */
    case 0x01: /* RTPS */
        MAC1 = A1(i44_add(i44_add(i44_add(i44_from((int64_t)TRX << 12), R11 * VX0), R12 * VY0), R13 * VZ0));
        MAC2 = A2(i44_add(i44_add(i44_add(i44_from((int64_t)TRY << 12), R21 * VX0), R22 * VY0), R23 * VZ0));
        MAC3 = A3(i44_add(i44_add(i44_add(i44_from((int64_t)TRZ << 12), R31 * VX0), R32 * VY0), R33 * VZ0));
        IR1 = (int16_t)Lm_B1(MAC1, lm);
        IR2 = (int16_t)Lm_B2(MAC2, lm);
        IR3 = (int16_t)Lm_B3_sf(m_mac3, m_sf, lm);
        SZ0 = SZ1; SZ1 = SZ2; SZ2 = SZ3;
        SZ3 = (uint16_t)Lm_D(m_mac3, 1);
        h_over_sz3 = (int32_t)Lm_E(gte_divide((uint16_t)H, SZ3));
        SXY0 = SXY1; SXY1 = SXY2;
        SX2 = (int16_t)Lm_G1(F((int64_t)OFX + ((int64_t)IR1 * h_over_sz3)) >> 16);
        SY2 = (int16_t)Lm_G2(F((int64_t)OFY + ((int64_t)IR2 * h_over_sz3)) >> 16);
        MAC0 = (int32_t)F((int64_t)DQB + ((int64_t)DQA * h_over_sz3));
        IR0 = (int16_t)Lm_H(m_mac0, 1);
        return 1;

    case 0x06: /* NCLIP */
        MAC0 = (int32_t)F((int64_t)(SX0 * SY1) + (SX1 * SY2) + (SX2 * SY0) - (SX0 * SY2) - (SX1 * SY0) - (SX2 * SY1));
        return 1;

    case 0x0c: /* OP */
        MAC1 = A1i((int64_t)(R22 * IR3) - (R33 * IR2));
        MAC2 = A2i((int64_t)(R33 * IR1) - (R11 * IR3));
        MAC3 = A3i((int64_t)(R11 * IR2) - (R22 * IR1));
        IR_OUT();
        return 1;

    case 0x10: /* DPCS */
        MAC1 = A1i((R << 16) + (IR0 * Lm_B1(A1i(((int64_t)RFC << 12) - (R << 16)), 0)));
        MAC2 = A2i((Gc << 16) + (IR0 * Lm_B2(A2i(((int64_t)GFC << 12) - (Gc << 16)), 0)));
        MAC3 = A3i((Bc << 16) + (IR0 * Lm_B3(A3i(((int64_t)BFC << 12) - (Bc << 16)), 0)));
        IR_OUT(); COLOR_OUT();
        return 1;

    case 0x11: /* INTPL */
        MAC1 = A1i((IR1 << 12) + (IR0 * Lm_B1(A1i(((int64_t)RFC << 12) - (IR1 << 12)), 0)));
        MAC2 = A2i((IR2 << 12) + (IR0 * Lm_B2(A2i(((int64_t)GFC << 12) - (IR2 << 12)), 0)));
        MAC3 = A3i((IR3 << 12) + (IR0 * Lm_B3(A3i(((int64_t)BFC << 12) - (IR3 << 12)), 0)));
        IR_OUT(); COLOR_OUT();
        return 1;

    case 0x12: /* MVMVA */
        mx = GTE_MX(gteop); v = GTE_V(gteop); cv = GTE_CV(gteop);
        switch (cv) {
        case 2:
            MAC1 = A1i((int64_t)(MX12(mx) * VY(v)) + (MX13(mx) * VZ(v)));
            MAC2 = A2i((int64_t)(MX22(mx) * VY(v)) + (MX23(mx) * VZ(v)));
            MAC3 = A3i((int64_t)(MX32(mx) * VY(v)) + (MX33(mx) * VZ(v)));
            Lm_B1(A1i(((int64_t)CV1(cv) << 12) + (MX11(mx) * VX(v))), 0);
            Lm_B2(A2i(((int64_t)CV2(cv) << 12) + (MX21(mx) * VX(v))), 0);
            Lm_B3(A3i(((int64_t)CV3(cv) << 12) + (MX31(mx) * VX(v))), 0);
            break;
        default:
            MAC1 = A1(i44_add(i44_add(i44_add(i44_from((int64_t)CV1(cv) << 12), MX11(mx) * VX(v)), MX12(mx) * VY(v)), MX13(mx) * VZ(v)));
            MAC2 = A2(i44_add(i44_add(i44_add(i44_from((int64_t)CV2(cv) << 12), MX21(mx) * VX(v)), MX22(mx) * VY(v)), MX23(mx) * VZ(v)));
            MAC3 = A3(i44_add(i44_add(i44_add(i44_from((int64_t)CV3(cv) << 12), MX31(mx) * VX(v)), MX32(mx) * VY(v)), MX33(mx) * VZ(v)));
            break;
        }
        IR_OUT();
        return 1;

    case 0x13: /* NCDS */
        LIGHT(VX0, VY0, VZ0); IR_OUT(); BKCOLOR(); IR_OUT(); FARCOL(); IR_OUT(); COLOR_OUT();
        return 1;

    case 0x14: /* CDP */
        BKCOLOR(); IR_OUT(); FARCOL(); IR_OUT(); COLOR_OUT();
        return 1;

    case 0x16: /* NCDT */
        for (v = 0; v < 3; v++) {
            LIGHT(VX(v), VY(v), VZ(v)); IR_OUT(); BKCOLOR(); IR_OUT(); FARCOL(); IR_OUT(); COLOR_OUT();
        }
        return 1;

    case 0x1b: /* NCCS */
        LIGHT(VX0, VY0, VZ0); IR_OUT(); BKCOLOR(); IR_OUT(); MULCOL(); IR_OUT(); COLOR_OUT();
        return 1;

    case 0x1c: /* CC */
        BKCOLOR(); IR_OUT(); MULCOL(); IR_OUT(); COLOR_OUT();
        return 1;

    case 0x1e: /* NCS */
        LIGHT(VX0, VY0, VZ0); IR_OUT(); BKCOLOR(); IR_OUT(); COLOR_OUT();
        return 1;

    case 0x20: /* NCT */
        for (v = 0; v < 3; v++) {
            LIGHT(VX(v), VY(v), VZ(v)); IR_OUT(); BKCOLOR(); IR_OUT(); COLOR_OUT();
        }
        return 1;

    case 0x28: /* SQR */
        MAC1 = A1i(IR1 * IR1); MAC2 = A2i(IR2 * IR2); MAC3 = A3i(IR3 * IR3);
        IR_OUT();
        return 1;

    case 0x1a: /* end of NCDT */
    case 0x29: /* DPCL */
        FARCOL(); IR_OUT(); COLOR_OUT();
        return 1;

    case 0x2a: /* DPCT */
        for (v = 0; v < 3; v++) {
            MAC1 = A1i((R0 << 16) + (IR0 * Lm_B1(A1i(((int64_t)RFC << 12) - (R0 << 16)), 0)));
            MAC2 = A2i((G0 << 16) + (IR0 * Lm_B2(A2i(((int64_t)GFC << 12) - (G0 << 16)), 0)));
            MAC3 = A3i((B0 << 16) + (IR0 * Lm_B3(A3i(((int64_t)BFC << 12) - (B0 << 16)), 0)));
            IR_OUT(); COLOR_OUT();
        }
        return 1;

    case 0x2d: /* AVSZ3 */
        MAC0 = (int32_t)F((int64_t)(ZSF3 * SZ1) + (ZSF3 * SZ2) + (ZSF3 * SZ3));
        OTZ = (uint16_t)Lm_D(m_mac0, 1);
        return 1;

    case 0x2e: /* AVSZ4 */
        MAC0 = (int32_t)F((int64_t)(ZSF4 * SZ0) + (ZSF4 * SZ1) + (ZSF4 * SZ2) + (ZSF4 * SZ3));
        OTZ = (uint16_t)Lm_D(m_mac0, 1);
        return 1;

    case 0x30: /* RTPT */
        for (v = 0; v < 3; v++) {
            MAC1 = A1(i44_add(i44_add(i44_add(i44_from((int64_t)TRX << 12), R11 * VX(v)), R12 * VY(v)), R13 * VZ(v)));
            MAC2 = A2(i44_add(i44_add(i44_add(i44_from((int64_t)TRY << 12), R21 * VX(v)), R22 * VY(v)), R23 * VZ(v)));
            MAC3 = A3(i44_add(i44_add(i44_add(i44_from((int64_t)TRZ << 12), R31 * VX(v)), R32 * VY(v)), R33 * VZ(v)));
            IR1 = (int16_t)Lm_B1(MAC1, lm);
            IR2 = (int16_t)Lm_B2(MAC2, lm);
            IR3 = (int16_t)Lm_B3_sf(m_mac3, m_sf, lm);
            SZ0 = SZ1; SZ1 = SZ2; SZ2 = SZ3;
            SZ3 = (uint16_t)Lm_D(m_mac3, 1);
            h_over_sz3 = (int32_t)Lm_E(gte_divide((uint16_t)H, SZ3));
            SXY0 = SXY1; SXY1 = SXY2;
            SX2 = (int16_t)Lm_G1(F((int64_t)OFX + ((int64_t)IR1 * h_over_sz3)) >> 16);
            SY2 = (int16_t)Lm_G2(F((int64_t)OFY + ((int64_t)IR2 * h_over_sz3)) >> 16);
        }
        MAC0 = (int32_t)F((int64_t)DQB + ((int64_t)DQA * h_over_sz3));
        IR0 = (int16_t)Lm_H(m_mac0, 1);
        return 1;

    case 0x3d: /* GPF */
        MAC1 = A1i(IR0 * IR1); MAC2 = A2i(IR0 * IR2); MAC3 = A3i(IR0 * IR3);
        IR_OUT(); COLOR_OUT();
        return 1;

    case 0x3e: /* GPL */
        MAC1 = A1i(gte_shift(MAC1, -m_sf) + (IR0 * IR1));
        MAC2 = A2i(gte_shift(MAC2, -m_sf) + (IR0 * IR2));
        MAC3 = A3i(gte_shift(MAC3, -m_sf) + (IR0 * IR3));
        IR_OUT(); COLOR_OUT();
        return 1;

    case 0x3f: /* NCCT */
        for (v = 0; v < 3; v++) {
            LIGHT(VX(v), VY(v), VZ(v)); IR_OUT(); BKCOLOR(); IR_OUT(); MULCOL(); IR_OUT(); COLOR_OUT();
        }
        return 1;
    }
    return 0;
}
