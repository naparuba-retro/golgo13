// Differential test: C MDEC port (runtime/video/mdec.c) vs MAME's original mdec.cpp on random macroblock streams.
#define _USE_MATH_DEFINES
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
extern "C" {
void mdec_reset(void); void mdec_dma_write(uint8_t *, uint32_t, int32_t); void mdec_dma_read(uint8_t *, uint32_t, int32_t);
void mdec_write(uint32_t, uint32_t); uint32_t mdec_read(uint32_t);
void ref_mdec_init(void); void ref_mdec_write(uint32_t, uint32_t); uint32_t ref_mdec_read(uint32_t);
void ref_mdec_dma_write(uint8_t *, uint32_t, int32_t); void ref_mdec_dma_read(uint8_t *, uint32_t, int32_t);
}
static uint64_t s = 0x9e3779b97f4a7c15ull;
static uint32_t rnd() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (uint32_t)(s >> 16); }
static uint8_t ramA[0x200000], ramB[0x200000];
static void both_cmd(uint32_t cmd, const uint32_t *data, int n, uint32_t addr)
{
    memcpy(ramA + addr, data, (size_t)n * 4); memcpy(ramB + addr, data, (size_t)n * 4);
    mdec_write(0, cmd); ref_mdec_write(0, cmd);
    mdec_dma_write(ramA, addr, n); ref_mdec_dma_write(ramB, addr, n);
}
int main(int argc, char **argv)
{
    int cases = argc > 1 ? atoi(argv[1]) : 300, bad = 0;
    mdec_reset(); ref_mdec_init();
    uint32_t cosw[32];
    for (int u = 0; u < 8; u++) for (int x = 0; x < 8; x += 2) {
        int a = u == 0 ? 23170 : (int)lround(32768.0 * cos((2 * x + 1) * u * M_PI / 16)), b = u == 0 ? 23170 : (int)lround(32768.0 * cos((2 * (x + 1) + 1) * u * M_PI / 16));
        if (a > 32767) a = 32767; if (b > 32767) b = 32767;
        cosw[(u * 8 + x) / 2] = (uint16_t)a | ((uint32_t)(uint16_t)b << 16);
    }
    both_cmd(0x60000000u, cosw, 32, 0x1000);
    for (int c = 0; c < cases; c++) {
        uint32_t q[32]; for (int i = 0; i < 32; i++) q[i] = (rnd() & 0x07070707u) | 0x01010101u;
        both_cmd(0x40000001u, q, 32, 0x2000);
        int mode15 = rnd() & 1, stp = rnd() & 1;
        uint32_t cmd = 0x30000000u | (mode15 ? (1u << 27) : 0) | (stp ? (1u << 25) : 0);
        uint16_t stream[1200]; int n = 0;
        int nmb = 1 + rnd() % 3;
        for (int mb = 0; mb < nmb; mb++) for (int blk = 0; blk < 6; blk++) {
            /* realistic amplitudes: values must stay inside the clamp tables (out-of-range indexes are undefined in both implementations) */
            stream[n++] = (uint16_t)((((rnd() % 8) + 1) << 10) | (((int)(rnd() % 81) - 40) & 0x3ff));     /* qscale + DC */
            int z = 0;
            for (;;) { int run = rnd() % 6; z += run + 1; if (z > 40 || rnd() % 9 == 0) break; stream[n++] = (uint16_t)((run << 10) | (((int)(rnd() % 17) - 8) & 0x3ff)); }
            stream[n++] = 0xfe00;
        }
        if (n & 1) stream[n++] = 0xfe00;
        int words = n / 2;
        memset(ramA, 0, sizeof ramA); memset(ramB, 0, sizeof ramB);
        memcpy(ramA + 0x10000, stream, (size_t)n * 2); memcpy(ramB + 0x10000, stream, (size_t)n * 2);
        mdec_write(0, cmd); ref_mdec_write(0, cmd);
        mdec_dma_write(ramA, 0x10000, words); ref_mdec_dma_write(ramB, 0x10000, words);
        int outwords = nmb * (mode15 ? 128 : 192);
        mdec_dma_read(ramA, 0x80000, outwords); ref_mdec_dma_read(ramB, 0x80000, outwords);
        if (memcmp(ramA + 0x80000, ramB + 0x80000, (size_t)outwords * 4) || mdec_read(1) != ref_mdec_read(1)) {
            bad++; static int bymode[2]; bymode[mode15]++;
            if (bad < 6) {
                int first = -1, ndiff = 0; for (int i = 0; i < outwords * 4; i++) if (ramA[0x80000 + i] != ramB[0x80000 + i]) { if (first < 0) first = i; ndiff++; }
                printf("MISMATCH case %d (mode15=%d stp=%d nmb=%d): first diff byte %d, %d bytes differ of %d; status %08x/%08x\n", c, mode15, stp, nmb, first, ndiff, outwords * 4, mdec_read(1), ref_mdec_read(1));
            }
            if (c == cases - 1 || bad == 223) {}
            if (c == cases - 1) printf("mismatches by mode: 24-bit %d, 15-bit %d\n", bymode[0], bymode[1]);
        }
    }
    printf("mdec differential: %d cases, %d mismatches\n", cases, bad);
    return bad ? 1 : 0;
}
