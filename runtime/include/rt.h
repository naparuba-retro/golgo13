/* Golgo 13 native port - runtime/generated-code contract (C11).
 *
 * Generated blocks are plain C functions; each executes one MIPS basic block
 * and returns the guest PC to continue at. A dispatcher loop calls them.
 * No CPU interpreter exists: an unknown target is reported by rt_unknown_pc().
 */
#ifndef GOLGO_RT_H
#define GOLGO_RT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* COP0 register indices used by generated code */
enum { CP0_BADVADDR = 8, CP0_SR = 12, CP0_CAUSE = 13, CP0_EPC = 14 };

/* Exception codes (CAUSE.ExcCode) */
enum { EXC_INT = 0, EXC_ADEL = 4, EXC_ADES = 5, EXC_SYS = 8, EXC_BP = 9, EXC_RI = 10, EXC_CPU = 11, EXC_OV = 12 };

typedef struct cpu {
    uint32_t r[32];
    uint32_t hi, lo;
    uint32_t cop0[32];
    uint64_t cycles;      /* incremented per block by the instruction count */
    void    *user;        /* runtime private state (memory, gte, devices...) */
} cpu_t;

typedef uint32_t (*blk_fn)(cpu_t *c);

/* ---- guest memory (implemented by the runtime; address = full 32-bit guest VA) ---- */
#ifdef RT_INLINE_MEM
/* Runtime build: RAM accesses are inlined into the generated blocks (little-endian host). Everything else (I/O, ROM,
 * banked ROM, misaligned = fatal, cache isolation) goes through the *_slow functions of the machine model. */
extern uint8_t *rt_ram; extern uint32_t rt_fast_limit, rt_fast_mask; extern int rt_isc;
uint32_t rt_rd8_slow (cpu_t *c, uint32_t a);
uint32_t rt_rd16_slow(cpu_t *c, uint32_t a);
uint32_t rt_rd32_slow(cpu_t *c, uint32_t a);
void     rt_wr8_slow (cpu_t *c, uint32_t a, uint32_t v);
void     rt_wr16_slow(cpu_t *c, uint32_t a, uint32_t v);
void     rt_wr32_slow(cpu_t *c, uint32_t a, uint32_t v);
#define RT_FASTP(a) (((a) & 0x1fffffffu) < rt_fast_limit)
#define RT_FASTO(a) ((a) & rt_fast_mask)
static inline uint32_t rt_rd8 (cpu_t *c, uint32_t a) { return (a < 0xc0000000u && RT_FASTP(a)) ? rt_ram[RT_FASTO(a)] : rt_rd8_slow(c, a); }
static inline uint32_t rt_rd16(cpu_t *c, uint32_t a) { return (a < 0xc0000000u && !(a & 1u) && RT_FASTP(a)) ? *(uint16_t *)(rt_ram + RT_FASTO(a)) : rt_rd16_slow(c, a); }
static inline uint32_t rt_rd32(cpu_t *c, uint32_t a) { return (a < 0xc0000000u && !(a & 3u) && RT_FASTP(a)) ? *(uint32_t *)(rt_ram + RT_FASTO(a)) : rt_rd32_slow(c, a); }
static inline void rt_wr8 (cpu_t *c, uint32_t a, uint32_t v) { if (a < 0xc0000000u && !rt_isc && RT_FASTP(a)) rt_ram[RT_FASTO(a)] = (uint8_t)v; else rt_wr8_slow(c, a, v); }
static inline void rt_wr16(cpu_t *c, uint32_t a, uint32_t v) { if (a < 0xc0000000u && !rt_isc && !(a & 1u) && RT_FASTP(a)) *(uint16_t *)(rt_ram + RT_FASTO(a)) = (uint16_t)v; else rt_wr16_slow(c, a, v); }
static inline void rt_wr32(cpu_t *c, uint32_t a, uint32_t v) { if (a < 0xc0000000u && !rt_isc && !(a & 3u) && RT_FASTP(a)) *(uint32_t *)(rt_ram + RT_FASTO(a)) = v; else rt_wr32_slow(c, a, v); }
#else
uint32_t rt_rd8 (cpu_t *c, uint32_t a);
uint32_t rt_rd16(cpu_t *c, uint32_t a);   /* a must be 2-aligned */
uint32_t rt_rd32(cpu_t *c, uint32_t a);   /* a must be 4-aligned */
void     rt_wr8 (cpu_t *c, uint32_t a, uint32_t v);
void     rt_wr16(cpu_t *c, uint32_t a, uint32_t v);
void     rt_wr32(cpu_t *c, uint32_t a, uint32_t v);
#endif

/* ---- COP0 ---- */
uint32_t rt_cop0_read (cpu_t *c, uint32_t n);
void     rt_cop0_write(cpu_t *c, uint32_t n, uint32_t v);
void     rt_rfe(cpu_t *c);

/* ---- exceptions: return the new PC (exception vector).
 * pc = address of the faulting instruction (the branch's address when in_slot). ---- */
uint32_t rt_exception(cpu_t *c, uint32_t code, uint32_t pc, uint32_t in_slot);

/* ---- GTE (COP2) ---- */
uint32_t rt_gte_mfc2(cpu_t *c, uint32_t n);      /* data registers  */
uint32_t rt_gte_cfc2(cpu_t *c, uint32_t n);      /* control registers */
void     rt_gte_mtc2(cpu_t *c, uint32_t n, uint32_t v);
void     rt_gte_ctc2(cpu_t *c, uint32_t n, uint32_t v);
void     rt_gte_cmd (cpu_t *c, uint32_t cmd25);  /* COP2 command, low 25 bits of the opcode */

/* ---- unknown / external control-flow target. Never emulates: reports and aborts
 * (or, for the BIOS kernel entry points 0xa0/0xb0/0xc0 etc, the runtime may provide native code). ---- */
uint32_t rt_unknown_pc(cpu_t *c, uint32_t pc);

/* ---- generated tables (game_table.c) ---- */
blk_fn code_lookup(uint32_t pc);     /* any image (game/bios/kern): NULL if pc is not a known block entry */
void   code_export_all(blk_fn *ram_tbl, uint32_t ram_words, blk_fn *rom_tbl, uint32_t rom_words);   /* flat dispatch tables (golgo13 set) */
void   rt_code_export_all(blk_fn *ram_tbl, uint32_t ram_words, blk_fn *rom_tbl, uint32_t rom_words);   /* the selected game's set */
int    game_code_select(const char *id);       /* 0 = ok, 1 = this build has no recompiled code for that game */
blk_fn game_lookup(uint32_t pc);    /* game image only (used by tests) */

#ifdef __cplusplus
}
#endif
#endif
