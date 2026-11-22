// Differential test: C GTE port (runtime/gte/gte.c) vs MAME's original gte.cpp on random states/commands.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
extern "C" {
#include "../runtime/gte/gte_port.h"
void ref_reset(void); void ref_setdr(int, uint32_t); void ref_setcr(int, uint32_t);
uint32_t ref_getdr(int); uint32_t ref_getcr(int); int ref_cmd(uint32_t); void ref_dump(uint32_t*, uint32_t*);
}
static uint64_t s = 88172645463325252ull;
static uint32_t rnd() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (uint32_t)(s >> 16); }
static uint32_t rval() {
    switch (rnd() % 6) {
    case 0: return rnd() & 0xff;
    case 1: return rnd() & 0xffff;
    case 2: return (uint32_t)(int32_t)(int16_t)rnd();
    case 3: return rnd();
    case 4: return (rnd() & 1) ? 0x7fff : 0xffff8000u;
    default: return rnd() & 0x1fffff;
    }
}
int main(int argc, char **argv) {
    long n = argc > 1 ? atol(argv[1]) : 200000; long bad = 0, unknown = 0;
    static const int fn[] = { 0x01,0x06,0x0c,0x10,0x11,0x12,0x13,0x14,0x16,0x1b,0x1c,0x1e,0x20,0x28,0x29,0x2a,0x2d,0x2e,0x30,0x3d,0x3e,0x3f,0x00,0x1a, 0x05, 0x3c };
    for (long it = 0; it < n; it++) {
        gte_reset(); ref_reset();
        for (int r = 0; r < 32; r++) { uint32_t v = rval(); gte_setdr(r, v); ref_setdr(r, v); }
        for (int r = 0; r < 32; r++) { uint32_t v = rval(); gte_setcr(r, v); ref_setcr(r, v); }
        uint32_t d1[32], c1[32], d2[32], c2[32];
        // compare after register writes
        memcpy(d1, gte_dr_raw(), 128); memcpy(c1, gte_cr_raw(), 128); ref_dump(d2, c2);
        if (memcmp(d1, d2, 128) || memcmp(c1, c2, 128)) { bad++; if (bad < 5) printf("MISMATCH after setup it=%ld\n", it); continue; }
        uint32_t op = (rnd() & 0x1ffffc0u) | fn[rnd() % (sizeof fn / sizeof fn[0])];
        int a = gte_docop2(op), b = ref_cmd(op);
        if (!a) unknown++;
        memcpy(d1, gte_dr_raw(), 128); memcpy(c1, gte_cr_raw(), 128); ref_dump(d2, c2);
        bool ok = a == b && !memcmp(d1, d2, 128) && !memcmp(c1, c2, 128);
        // also compare getters (they canonicalise some registers)
        for (int r = 0; r < 32 && ok; r++) { if (gte_getdr(r) != ref_getdr(r)) ok = false; if (gte_getcr(r) != ref_getcr(r)) ok = false; }
        if (!ok) { bad++; if (bad < 6) { printf("MISMATCH op=%07x it=%ld ret %d/%d\n", op, it, a, b);
            for (int r = 0; r < 32; r++) if (d1[r] != d2[r]) printf("  dr%d port=%08x ref=%08x\n", r, d1[r], d2[r]);
            for (int r = 0; r < 32; r++) if (c1[r] != c2[r]) printf("  cr%d port=%08x ref=%08x\n", r, c1[r], c2[r]); } }
    }
    printf("gte differential: %ld cases, %ld mismatches (%ld unknown-op cases)\n", n, bad, unknown);
    return bad ? 1 : 0;
}
