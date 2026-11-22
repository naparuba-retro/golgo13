/* PlayStation MDEC (motion decoder: run-length + IDCT + YUV->RGB), C port of MAME src/devices/cpu/psx/mdec.cpp
 * (license:BSD-3-Clause, Copyright 2003-2011 smf). Used by the DMA channels 0 (in) and 1 (out). */
#include <string.h>
#include "mdec.h"

#define DCTSIZE2 64
#define COS_BITS 21

static const uint8_t zigzag[DCTSIZE2] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

uint64_t mdec_blocks;                       /* statistics: decoded macroblocks */
static uint32_t n_decoded, n_offset, n0_command, n0_address, n0_size, n1_command, n1_status;
static uint16_t out[24 * 16];
static int32_t q_y[DCTSIZE2], q_uv[DCTSIZE2], cosv[DCTSIZE2], cos_pre[DCTSIZE2 * DCTSIZE2];
static uint16_t clamp8[256 * 3], r5[256 * 3], g5[256 * 3], b5[256 * 3];
static int32_t unpacked[DCTSIZE2 * 6 * 2];

void mdec_reset(void)
{
    n0_command = n0_address = n0_size = n1_command = n1_status = n_offset = n_decoded = 0;
    for (int n = 0; n < 256; n++) {
        clamp8[n] = 0; clamp8[n + 256] = (uint16_t)n; clamp8[n + 512] = 255;
        r5[n] = 0; r5[n + 256] = (uint16_t)(n >> 3); r5[n + 512] = 255 >> 3;
        g5[n] = 0; g5[n + 256] = (uint16_t)((n >> 3) << 5); g5[n + 512] = (255 >> 3) << 5;
        b5[n] = 0; b5[n + 256] = (uint16_t)((n >> 3) << 10); b5[n + 512] = (255 >> 3) << 10;
    }
}

static void cos_precalc(void)
{
    int32_t *p = cos_pre;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++)
        *p++ = (int32_t)(((int64_t)cosv[u * 8 + x] * cosv[v * 8 + y]) >> (30 - COS_BITS));
}

static void idct(const int32_t *src, int32_t *dst)
{
    const int32_t *pre = cos_pre;
    for (int yx = 0; yx < DCTSIZE2; yx++) {
        int32_t z[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        const int32_t *data = src;
        for (int vu = 0; vu < DCTSIZE2 / 8; vu++) {
            for (int k = 0; k < 8; k++) z[k] += data[k] * pre[k];
            data += 8; pre += 8;
        }
        *dst++ = (z[0] + z[1] + z[2] + z[3] + z[4] + z[5] + z[6] + z[7]) >> (COS_BITS + 2);
    }
}

static inline uint16_t rd16(const uint8_t *ram, uint32_t a) { return (uint16_t)(ram[a & 0x1fffff] | (ram[(a + 1) & 0x1fffff] << 8)); }
static inline int32_t sext10(uint16_t v) { return (int32_t)((v & 0x3ff) ^ 0x200) - 0x200; }

static uint32_t unpack(const uint8_t *ram, uint32_t addr)
{
    int32_t *q = q_uv, *block = unpacked, tmp[DCTSIZE2];
    for (int n = 0; n < 6; n++) {
        memset(tmp, 0, sizeof tmp);
        if (n == 2) q = q_y;
        uint16_t packed = rd16(ram, addr); addr += 2;
        if (packed == 0xfe00) break;
        int32_t qscale = packed >> 10;
        tmp[0] = sext10(packed) * q[0];
        uint8_t z = 0;
        for (;;) {
            packed = rd16(ram, addr); addr += 2;
            if (packed == 0xfe00) break;
            z = (uint8_t)(z + (packed >> 10) + 1);
            if (z > 63) break;
            tmp[zigzag[z]] = (sext10(packed) * q[z] * qscale) / 8;
        }
        idct(tmp, block); block += DCTSIZE2;
    }
    return addr;
}

static inline int32_t cr_r(int32_t c) { return (1435 * c) >> 10; }
static inline int32_t cr_g(int32_t c) { return (-731 * c) >> 10; }
static inline int32_t cb_g(int32_t c) { return (-351 * c) >> 10; }
static inline int32_t cb_b(int32_t c) { return (1814 * c) >> 10; }

static void rgb15(uint32_t a, int32_t r, int32_t g, int32_t b, const int32_t *y, uint16_t stp)
{
    out[a / 2]     = (uint16_t)(stp | r5[y[0] + r + 128 + 256] | g5[y[0] + g + 128 + 256] | b5[y[0] + b + 128 + 256]);
    out[a / 2 + 1] = (uint16_t)(stp | r5[y[1] + r + 128 + 256] | g5[y[1] + g + 128 + 256] | b5[y[1] + b + 128 + 256]);
}
static void yuv_to_rgb15(void)
{
    uint16_t stp = (n0_command & (1u << 25)) ? 0x8000 : 0; int addr = 0;
    int32_t *cr = &unpacked[0], *cb = &unpacked[DCTSIZE2], *y = &unpacked[DCTSIZE2 * 2];
    for (int z = 0; z < 2; z++) {
        for (int yy = 0; yy < 4; yy++) {
            for (int x = 0; x < 4; x++) {
                int32_t c_r = *cr, c_b = *cb, r = cr_r(c_r), g = cr_g(c_r) + cb_g(c_b), b = cb_b(c_b);
                rgb15((uint32_t)(addr + 0), r, g, b, y, stp); rgb15((uint32_t)(addr + 32), r, g, b, y + 8, stp);
                c_r = *(cr + 4); c_b = *(cb + 4); r = cr_r(c_r); g = cr_g(c_r) + cb_g(c_b); b = cb_b(c_b);
                rgb15((uint32_t)(addr + 16), r, g, b, y + DCTSIZE2, stp); rgb15((uint32_t)(addr + 48), r, g, b, y + DCTSIZE2 + 8, stp);
                cr++; cb++; y += 2; addr += 4;
            }
            cr += 4; cb += 4; y += 8; addr += 48;
        }
        y += DCTSIZE2;
    }
    n_decoded = (16 * 16) / 2;
}
static void rgb24(uint32_t a, int32_t r, int32_t g, int32_t b, const int32_t *y)
{
    out[a / 2]     = (uint16_t)((clamp8[y[0] + g + 128 + 256] << 8) | clamp8[y[0] + r + 128 + 256]);
    out[a / 2 + 1] = (uint16_t)((clamp8[y[1] + r + 128 + 256] << 8) | clamp8[y[0] + b + 128 + 256]);
    out[a / 2 + 2] = (uint16_t)((clamp8[y[1] + b + 128 + 256] << 8) | clamp8[y[1] + g + 128 + 256]);
}
static void yuv_to_rgb24(void)
{
    int addr = 0;
    int32_t *cr = &unpacked[0], *cb = &unpacked[DCTSIZE2], *y = &unpacked[DCTSIZE2 * 2];
    for (int z = 0; z < 2; z++) {
        for (int yy = 0; yy < 4; yy++) {
            for (int x = 0; x < 4; x++) {
                int32_t c_r = *cr, c_b = *cb, r = cr_r(c_r), g = cr_g(c_r) + cb_g(c_b), b = cb_b(c_b);
                rgb24((uint32_t)(addr + 0), r, g, b, y); rgb24((uint32_t)(addr + 48), r, g, b, y + 8);
                c_r = *(cr + 4); c_b = *(cb + 4); r = cr_r(c_r); g = cr_g(c_r) + cb_g(c_b); b = cb_b(c_b);
                rgb24((uint32_t)(addr + 24), r, g, b, y + DCTSIZE2); rgb24((uint32_t)(addr + 72), r, g, b, y + DCTSIZE2 + 8);
                cr++; cb++; y += 2; addr += 6;
            }
            cr += 4; cb += 4; y += 8; addr += 72;
        }
        y += DCTSIZE2;
    }
    n_decoded = (24 * 16) / 2;
}

void mdec_dma_write(uint8_t *ram, uint32_t address, int32_t size)       /* channel 0: RAM -> MDEC */
{
    const uint32_t *w = (const uint32_t *)ram;
    switch (n0_command >> 28) {
    case 0x3: n0_address = address; n0_size = (uint32_t)size * 4; n1_status |= 1u << 29; break;
    case 0x4: {
        int idx = 0;
        while (size > 0) {
            uint32_t v = w[(address & 0x1fffff) / 4];
            if (idx < DCTSIZE2) { q_y[idx] = v & 0xff; q_y[idx + 1] = (v >> 8) & 0xff; q_y[idx + 2] = (v >> 16) & 0xff; q_y[idx + 3] = v >> 24; }
            else if (idx < DCTSIZE2 * 2) { int i = idx - DCTSIZE2; q_uv[i] = v & 0xff; q_uv[i + 1] = (v >> 8) & 0xff; q_uv[i + 2] = (v >> 16) & 0xff; q_uv[i + 3] = v >> 24; }
            idx += 4; address += 4; size--;
        }
        break; }
    case 0x6: {
        int idx = 0;
        while (size > 0) {
            uint32_t v = w[(address & 0x1fffff) / 4];
            if (idx + 1 < DCTSIZE2) { cosv[idx] = (int16_t)(v & 0xffff); cosv[idx + 1] = (int16_t)(v >> 16); }
            idx += 2; address += 4; size--;
        }
        cos_precalc();
        break; }
    default: break;
    }
}

void mdec_dma_read(uint8_t *ram, uint32_t address, int32_t size)        /* channel 1: MDEC -> RAM */
{
    if ((n0_command & (1u << 29)) != 0 && n0_size != 0) {
        while (size > 0) {
            if (n_decoded == 0) {
                if ((int32_t)n0_size <= 0) { n0_size = 0; break; }
                mdec_blocks++;
                uint32_t next = unpack(ram, n0_address);
                n0_size -= next - n0_address; n0_address = next;
                if (n0_command & (1u << 27)) yuv_to_rgb15(); else yuv_to_rgb24();
                n_offset = 0;
                while (rd16(ram, n0_address) == 0xfe00 && n0_size) { n0_address += 2; n0_size -= 2; }
            }
            uint32_t n = n_decoded; if (n > (uint32_t)size) n = (uint32_t)size;
            n_decoded -= n;
            for (uint32_t i = 0; i < n * 4; i++) ram[(address + i) & 0x1fffff] = ((const uint8_t *)out)[n_offset + i];
            n_offset += n * 4; address += n * 4; size -= (int32_t)n;
        }
    }
    if ((int32_t)n0_size <= 0) n1_status &= ~(1u << 29);
}

void mdec_write(uint32_t offset, uint32_t data) { if (offset == 0) n0_command = data; else if (offset == 1) n1_command = data; }
uint32_t mdec_read(uint32_t offset) { return offset == 1 ? n1_status : 0; }
