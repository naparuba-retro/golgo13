/* Differential test harness: runs generated blocks on given register/memory states.
 * Memory model: word value = fmix32(addr ^ seed) unless overwritten (overlay). Only accesses in
 * the window [WIN_LO, WIN_HI) and properly aligned are valid; otherwise the case is flagged wild. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt.h"

#define WIN_LO 0x80400000u
#define WIN_HI 0x80800000u
static uint32_t g_seed; static int g_wild;
static struct { uint32_t a, v; } ov[256]; static int nov;
static struct { uint32_t a, sz, v; } wl[256]; static int nwl;

static uint32_t fmix(uint32_t h){ h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16; return h; }
uint32_t mem_word(uint32_t a, uint32_t seed){ return fmix((a & ~3u) ^ (seed * 0x9e3779b9u)); }
static uint32_t mget(uint32_t a){ a &= ~3u; for (int i = nov - 1; i >= 0; i--) if (ov[i].a == a) return ov[i].v; return mem_word(a, g_seed); }
static void mset(uint32_t a, uint32_t v){ a &= ~3u; for (int i = 0; i < nov; i++) if (ov[i].a == a){ ov[i].v = v; return; } if (nov < 256){ ov[nov].a = a; ov[nov].v = v; nov++; } }
static int chk(uint32_t a, uint32_t al, uint32_t sz){ if (a < WIN_LO || a + sz > WIN_HI || (a & (al - 1))) { g_wild = 1; return 0; } return 1; }
uint32_t rt_rd8 (cpu_t *c, uint32_t a){ (void)c; if(!chk(a,1,1)) return 0; return (mget(a) >> (8*(a&3))) & 0xff; }
uint32_t rt_rd16(cpu_t *c, uint32_t a){ (void)c; if(!chk(a,2,2)) return 0; return (mget(a) >> (8*(a&2))) & 0xffff; }
uint32_t rt_rd32(cpu_t *c, uint32_t a){ (void)c; if(!chk(a,4,4)) return 0; return mget(a); }
static void logw(uint32_t a, uint32_t sz, uint32_t v){ if (nwl < 256){ wl[nwl].a=a; wl[nwl].sz=sz; wl[nwl].v=v; nwl++; } }
void rt_wr8 (cpu_t *c, uint32_t a, uint32_t v){ (void)c; if(!chk(a,1,1)) return; v&=0xff; uint32_t w=mget(a),s=8*(a&3); mset(a,(w&~(0xffu<<s))|(v<<s)); logw(a,1,v); }
void rt_wr16(cpu_t *c, uint32_t a, uint32_t v){ (void)c; if(!chk(a,2,2)) return; v&=0xffff; uint32_t w=mget(a),s=8*(a&2); mset(a,(w&~(0xffffu<<s))|(v<<s)); logw(a,2,v); }
void rt_wr32(cpu_t *c, uint32_t a, uint32_t v){ (void)c; if(!chk(a,4,4)) return; mset(a,v); logw(a,4,v); }
uint32_t rt_cop0_read(cpu_t *c, uint32_t n){ (void)c;(void)n; g_wild = 1; return 0; }
void rt_cop0_write(cpu_t *c, uint32_t n, uint32_t v){ (void)c;(void)n;(void)v; g_wild = 1; }
void rt_rfe(cpu_t *c){ (void)c; g_wild = 1; }
uint32_t rt_exception(cpu_t *c, uint32_t code, uint32_t pc, uint32_t s){ (void)c;(void)code;(void)pc;(void)s; g_wild = 1; return 0xbad00000u; }
uint32_t rt_gte_mfc2(cpu_t *c, uint32_t n){ (void)c;(void)n; g_wild = 1; return 0; }
uint32_t rt_gte_cfc2(cpu_t *c, uint32_t n){ (void)c;(void)n; g_wild = 1; return 0; }
void rt_gte_mtc2(cpu_t *c, uint32_t n, uint32_t v){ (void)c;(void)n;(void)v; g_wild = 1; }
void rt_gte_ctc2(cpu_t *c, uint32_t n, uint32_t v){ (void)c;(void)n;(void)v; g_wild = 1; }
void rt_gte_cmd(cpu_t *c, uint32_t x){ (void)c;(void)x; g_wild = 1; }
uint32_t rt_unknown_pc(cpu_t *c, uint32_t pc){ (void)c; (void)pc; g_wild = 1; return 0xdead0000u; }

int main(int argc, char **argv){
    if (argc < 3) return 2;
    FILE *fi = fopen(argv[1], "rb"), *fo = fopen(argv[2], "wb");
    if (!fi || !fo) return 3;
    uint32_t in[37];  /* pc, regs[32] (r0 slot unused), hi, lo, seed */
    static cpu_t cpu;
    while (fread(in, 4, 36, fi) == 36) {
        uint32_t pc = in[0]; memset(&cpu, 0, sizeof cpu);
        for (int i = 1; i < 32; i++) cpu.r[i] = in[i];   /* in[1..31] */
        cpu.hi = in[32]; cpu.lo = in[33]; g_seed = in[34]; nov = 0; nwl = 0; g_wild = 0;
        blk_fn f = game_lookup(pc);
        uint32_t nx = f ? f(&cpu) : 0xffffffffu;
        uint32_t out[36 + 1 + 3 * 256]; int k = 0;
        out[k++] = nx; out[k++] = g_wild; out[k++] = cpu.hi; out[k++] = cpu.lo; out[k++] = nwl;
        for (int i = 0; i < 32; i++) out[k++] = cpu.r[i];
        fwrite(out, 4, k, fo);
        for (int i = 0; i < nwl; i++){ uint32_t w[3] = { wl[i].a, wl[i].sz, wl[i].v }; fwrite(w, 4, 3, fo); }
    }
    fclose(fo); return 0;
}
