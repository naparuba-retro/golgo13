/* Machine model: memory map, COP0/exceptions/interrupts, IRQ controller, DMA, root counters, GPU status (stub),
 * board glue (ROM banking, shared RAM, EEPROM). See machine.h for sources. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "machine.h"
#include "sys10.h"
#include "spu.h"
#include "mp3.h"
static uint64_t sys10_next_io;
#include "h8_machine.h"
#include "gpu.h"
#include "c352.h"
#include "mdec.h"

#define H8_SYNC_CYCLES 400u
static uint16_t c352_store[0x4000];

machine_t G;
cpu_t CPU;

#define SR_IEC   0x00000001u
#define SR_ISC   0x00010000u
#define SR_BEV   0x00400000u

/* ------------------------------------------------------------------ logging */
void log_once(uint32_t key, const char *fmt, ...)
{
    for (int i = 0; i < G.nseen; i++) if (G.seen[i] == key) return;
    if (G.nseen < 512) G.seen[G.nseen++] = key;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

/* ------------------------------------------------------------------ small helpers */
static inline uint32_t rd_le(const uint8_t *b, int sz)
{
    uint32_t v = 0; for (int i = 0; i < sz; i++) v |= (uint32_t)b[i] << (8 * i); return v;
}
static inline void wr_le(uint8_t *b, int sz, uint32_t v)
{
    for (int i = 0; i < sz; i++) b[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t shift_in(uint32_t reg, uint32_t addr, int sz, uint32_t v)   /* sub-word write into a 32-bit register */
{
    uint32_t sh = (addr & 3) * 8, m = (sz == 4) ? 0xffffffffu : (((1u << (8 * sz)) - 1) << sh);
    return sz == 4 ? v : ((reg & ~m) | ((v << sh) & m));
}
static uint32_t shift_out(uint32_t reg, uint32_t addr, int sz) { return sz == 4 ? reg : (reg >> ((addr & 3) * 8)) & ((1u << (8 * sz)) - 1); }

/* ------------------------------------------------------------------ IRQ controller (irq.cpp) */
static void irq_update(void)
{
    if (G.irq_stat & G.irq_mask) CPU.cop0[CP0_CAUSE] |= 1u << 10; else CPU.cop0[CP0_CAUSE] &= ~(1u << 10);
}
static void irq_set(uint32_t bits) { G.irq_stat |= bits; irq_update(); }
static void spu_irq9(void) { irq_set(1u << 9); }

/* ------------------------------------------------------------------ GPU glue (rendering in runtime/gpu/gpu.c) */
static uint64_t frame_cycles(void) { return CPU_HZ / VBLANK_HZ; }
static uint32_t gpu_status_read(void)
{
    uint64_t line = frame_cycles() / 262, vpos = (CPU.cycles % frame_cycles()) / line;
    return gpu_status(G.frame, (uint32_t)vpos);
}
static void gpu_write32(uint32_t reg, uint32_t data)
{
    if (reg == 0) { gpu_gp0_write(data); G.gp0_words++; } else gpu_gp1_write(data);
}

/* ------------------------------------------------------------------ DMA (dma.cpp) */
static void dma_interrupt_update(void)
{
    uint32_t n_int = (G.dicr >> 24) & 0x7f, n_mask = (G.dicr >> 16) & 0xff;
    if ((n_mask & 0x80) && (n_int & n_mask)) { G.dicr |= 0x80000000u; irq_set(1u << 3); }
    G.dicr &= 0x00ffffffu | (G.dicr << 8);
}
static uint32_t *ram32(void) { return (uint32_t *)G.ram; }

/* namcos12_rom_read(): DMA from the main ROM or the banked ROMs into RAM (read handler of channel 5) */
static void dma5_rom_read(uint32_t address, int32_t size)
{
    int32_t offset = (int32_t)G.dmaoffset;
    int is_main = ((uint32_t)offset >> 31) & 1;
    if (G.exp_base == 0x1f300000u) is_main = 1;
    const uint8_t *region; uint32_t rsize;
    if (is_main) { region = G.rom_main; rsize = 0x400000; offset &= 0xffffff; }
    else {
        region = G.rom_bank; rsize = 0x3c00000; offset &= 0x7fffffff;
        offset += (int32_t)(G.bankoffset * 0x200000u);
    }
    if (gpu_trace_loads) fprintf(stderr, "ROMRD f=%llu %s off=%08x bank=%u bytes=%d ram=%08x\n", (unsigned long long)gpu_frame_no, is_main ? "main" : "bank", (unsigned)(offset & 0x7fffffff), (unsigned)G.bankoffset, size * 4, address);
    address /= 4; offset /= 2;
    int32_t romleft = ((int32_t)(rsize / 2) - offset) / 2;
    if (size > romleft) size = romleft;
    int32_t ramleft = (int32_t)(RAM_BYTES / 4) - (int32_t)address;
    if (size > ramleft) size = ramleft;
    const uint16_t *src = (const uint16_t *)region;
    while (size > 0) {
        ram32()[address] = (uint32_t)src[offset] | ((uint32_t)src[offset + 1] << 16);
        address++; offset += 2; size--;
    }
}

static void gpu_consume(uint32_t addr, uint32_t nwords)
{
    gpu_src_addr = addr;
    uint32_t i0 = (addr & (RAM_BYTES - 1)) / 4;
    for (uint32_t i = 0; i < nwords; i++) gpu_gp0_write(ram32()[(i0 + i) & (RAM_BYTES / 4 - 1)]);
    G.gp0_words += nwords;
}

static void dma_finished(int idx)
{
    dma_chan_t *d = &G.dma[idx];
    uint32_t adrmask = RAM_BYTES - 1;
    if (d->chcr == 0x01000201u) {
        int32_t size = (int32_t)d->bcr;
        if (d->chcr & 0x200) { uint32_t ba = d->bcr >> 16; if (!ba) ba = 0x10000; size = (int32_t)((d->bcr & 0xffff) * ba); }
        if (idx == 2) { G.dma2_blocks++; gpu_consume(d->base & adrmask, (uint32_t)size); }
    } else if (d->chcr == 0x01000401u && idx == 2) {
        uint32_t addr = d->base & 0xffffff, total = 0;
        G.dma2_lists++;
        if (addr != 0xffffff) {
            for (;;) {
                if (addr == 0xffffff) break;
                if (total > 65535) break;
                addr &= adrmask;
                uint32_t next = ram32()[addr / 4], size = next >> 24;
                gpu_consume(addr + 4, size);
                if ((next & 0xffffff) != 0xffffff) {
                    if (addr == ram32()[(next & adrmask) / 4] || addr == (next & adrmask)) break;
                }
                addr = next & 0xffffff; total += size + 1;
            }
        }
    }
    d->chcr &= ~((1u << 24) | (1u << 28));
    G.dicr |= 1u << (24 + idx);
    dma_interrupt_update();
}

static void dma_write(uint32_t off, uint32_t data)   /* off in 32-bit units from 0x1f801080 */
{
    int idx = (int)(off / 4);
    if (idx < 7) {
        dma_chan_t *d = &G.dma[idx];
        switch (off % 4) {
        case 0: d->base = data; break;
        case 1: d->bcr = data; break;
        default:
            d->chcr = data;
            if ((d->chcr & (1u << 24)) && (G.dpcr & (1u << (3 + idx * 4)))) {
                uint32_t addr = d->base & (RAM_BYTES - 1);
                int32_t size = (int32_t)d->bcr;
                if (d->chcr & 0x200) { uint32_t ba = d->bcr >> 16; if (!ba) ba = 0x10000; size = (int32_t)((d->bcr & 0xffff) * ba); }
                if (d->chcr == 0x01000000u && idx == 5) { if (G.sys10) sys10_dma5(ram32(), addr, size); else dma5_rom_read(addr, size); dma_finished(idx); }
                else if (idx == 4 && G.sys10 && (d->chcr == 0x01000201u || d->chcr == 0x01000200u)) {   /* SPU DMA */
                    int32_t left = (int32_t)(RAM_BYTES / 4 - addr / 4); if (size > left) size = left;
                    if (d->chcr == 0x01000201u) spu_dma_write(ram32() + addr / 4, size); else spu_dma_read(ram32() + addr / 4, size);
                    dma_finished(idx);
                }
                else if (d->chcr == 0x01000200u && idx == 2) {   /* VRAM -> RAM : no VRAM yet, zero fill */
                    int32_t left = (int32_t)(RAM_BYTES / 4 - addr / 4); if (size > left) size = left;
                    for (int32_t i = 0; i < size; i++) ram32()[addr / 4 + i] = gpu_read();
                    dma_finished(idx);
                }
                else if ((d->chcr == 0x01000201u && idx == 2) || (d->chcr == 0x01000401u && idx == 2)) dma_finished(idx);
                else if (d->chcr == 0x11000002u && idx == 6) {   /* ordering-table clear (reverse) */
                    int32_t n = (int32_t)(d->bcr & 0xffff); if (!n) n = 0x10000; n--;
                    uint32_t a = addr;
                    while (n > 0) { uint32_t nx = (a - 4) & 0xffffff; ram32()[a / 4] = nx; a = nx; n--; }
                    ram32()[a / 4] = 0xffffff;
                    dma_finished(idx);
                } else if (idx == 0 && d->chcr == 0x01000201u) { mdec_dma_write(G.ram, addr, size); dma_finished(idx);
                } else if (idx == 1 && d->chcr == 0x01000200u) { mdec_dma_read(G.ram, addr, size); dma_finished(idx);
                } else log_once(0x4d410000u + idx * 16 + 1, "[dma] ch%d unsupported mode chcr=%08x\n", idx, d->chcr);
            }
        }
    } else {
        if (off % 4 == 0) G.dpcr = data;
        else if (off % 4 == 1) {
            uint32_t dicr = (G.dicr & 0x80000000u) | (G.dicr & ~data & 0x7f000000u) | (data & 0xff803fu);
            G.dicr = dicr;
            if ((G.dicr & 0x80000000u) && (G.dicr & 0x7f000000u) == 0) { G.dicr &= ~0x80000000u; }
        }
    }
}
static uint32_t dma_read(uint32_t off)
{
    int idx = (int)(off / 4);
    if (idx < 7) { dma_chan_t *d = &G.dma[idx]; switch (off % 4) { case 0: return d->base; case 1: return d->bcr; default: return d->chcr; } }
    return (off % 4 == 0) ? G.dpcr : (off % 4 == 1) ? G.dicr : 0;
}

/* ------------------------------------------------------------------ root counters (rcnt.cpp) */
enum { RC_RESET = 0x0004, RC_STOP = 0x0001, RC_COUNTTARGET = 0x0008, RC_IRQTARGET = 0x0010, RC_IRQOVERFLOW = 0x0020,
       RC_REPEAT = 0x0040, RC_CLC = 0x0100, RC_DIV = 0x0200, RC_REACHEDTARGET = 0x0800, RC_REACHEDFFFF = 0x1000 };
/* bit values follow MAME rcnt.h */
static int rc_divider(int n)
{
    uint32_t m = G.rc[n].mode;
    if (n == 0 && (m & RC_CLC)) return 5;
    if (n == 1 && (m & RC_CLC)) return 2150;
    if (n == 2 && (m & RC_DIV)) return 8;
    return 1;
}
static uint64_t rc_now(void) { return CPU.cycles * PSX_CLK_HZ / CPU_HZ; }   /* time in PSX-clock ticks */
static uint16_t rc_current(int n)
{
    rcnt_t *r = &G.rc[n];
    if (r->mode & RC_STOP) return (uint16_t)r->count;
    uint64_t cur = (rc_now() - r->start) / (uint64_t)rc_divider(n) + r->count;
    if (cur > 0xffff) { r->count = (uint32_t)cur; r->start = rc_now(); r->mode |= RC_REACHEDFFFF; }
    return (uint16_t)cur;
}
static void rc_adjust(int n)
{
    rcnt_t *r = &G.rc[n];
    if (r->mode & RC_STOP) { r->armed = 0; return; }
    int target = (r->mode & (RC_COUNTTARGET | RC_IRQTARGET)) ? (int)r->target : 0x10000;
    int dur = target - (int)rc_current(n);
    if (dur < 1) { dur += 0x10000; r->mode |= (r->mode & RC_COUNTTARGET) ? RC_REACHEDTARGET : RC_REACHEDFFFF; }
    uint64_t ticks = (uint64_t)dur * (uint64_t)rc_divider(n);
    r->due = CPU.cycles + (ticks * CPU_HZ + PSX_CLK_HZ - 1) / PSX_CLK_HZ;
    r->armed = 1;
}
static void rc_fire(int n)
{
    rcnt_t *r = &G.rc[n];
    r->count = 0; r->start = rc_now(); r->armed = 0;
    if (r->mode & RC_REPEAT) rc_adjust(n);
    if (r->mode & (RC_IRQOVERFLOW | RC_IRQTARGET)) irq_set(1u << (4 + n));
}
static uint32_t rc_read(uint32_t off)
{
    int n = (int)(off / 4); if (n > 2) return 0;
    rcnt_t *r = &G.rc[n]; uint32_t data;
    switch (off % 4) {
    case 0: data = rc_current(n); break;
    case 1: data = r->mode; r->mode &= ~(RC_REACHEDFFFF | RC_REACHEDTARGET); break;
    case 2: data = r->target; break;
    default: return 0;
    }
    return data;
}
static void rc_write(uint32_t off, uint32_t data)
{
    int n = (int)(off / 4); if (n > 2) return;
    rcnt_t *r = &G.rc[n];
    switch (off % 4) {
    case 0: r->count = data; r->start = rc_now(); break;
    case 1:
        r->count = rc_current(n); r->start = rc_now();
        if (data & RC_RESET) { data &= ~(RC_RESET | RC_STOP); r->count = 0; }
        data &= ~(RC_REACHEDFFFF | RC_REACHEDTARGET);
        r->mode = data | (r->mode & (RC_REACHEDFFFF | RC_REACHEDTARGET));
        break;
    case 2: r->target = data; break;
    default: return;
    }
    rc_adjust(n);
}

/* ------------------------------------------------------------------ memory map */
static void fast_mem_update(void);
static uint32_t ram_window(void)
{
    switch ((G.ram_config >> 8) & 0xf) {
    case 0x0: case 0x1: return 0x100000;
    case 0x4: case 0x8: case 0x9: return 0x200000;
    case 0x2: case 0x3: case 0x5: case 0x6: case 0xc: return 0x400000;
    case 0x7: case 0xa: case 0xb: case 0xd: case 0xe: return 0x800000;
    default: return 0x1000000;
    }
}

static uint32_t hw_read(uint32_t p, int sz)
{
    if (p >= 0x1f801070 && p < 0x1f801078) { uint32_t r = (p & 4) ? G.irq_mask : G.irq_stat; return shift_out(r, p, sz); }
    if (p >= 0x1f801080 && p < 0x1f801100) return shift_out(dma_read((p - 0x1f801080) >> 2), p, sz);
    if (p >= 0x1f801100 && p < 0x1f801130) return shift_out(rc_read((p - 0x1f801100) >> 2), p, sz);
    if (p >= 0x1f801810 && p < 0x1f801818) {
        uint32_t v = (p & 4) ? gpu_status_read() : gpu_read();
        return shift_out(v, p, sz);
    }
    if (p == 0x1f801060) return shift_out(G.ram_config, p, sz);
    if (p == 0x1f801010) return shift_out(G.rom_config, p, sz);
    if (p == 0x1f801000) return shift_out(G.exp_base, p, sz);
    if (p >= 0x1f801040 && p < 0x1f801060) {   /* SIO0 / SIO1: no device attached; status = TX ready + TX empty (the BIOS console polls it) */
        if ((p & 0xf) >= 4 && (p & 0xf) < 8) return shift_out(0x5u, p, sz);
        log_once(p, "[io] SIO read %08x (stub 0)\n", p); return 0;
    }
    if (p >= 0x1f801800 && p < 0x1f801804) { return 0; }
    if (p >= 0x1f801820 && p < 0x1f801828) return shift_out(mdec_read((p >> 2) & 1), p, sz);
    if (p >= 0x1f801c00 && p < 0x1f802000) { return G.sys10 ? spu_read(p, sz) : 0; }   /* SPU window (System 10 only; System 12 has the C352) */
    if (p - 0x1f801000 < sizeof G.hwram) return rd_le(&G.hwram[p - 0x1f801000], sz);
    log_once(p, "[io] unmapped read %08x size %d\n", p, sz);
    return 0;
}
static void hw_write(uint32_t p, int sz, uint32_t v)
{
    if (p >= 0x1f801070 && p < 0x1f801078) {
        if (p & 4) { G.irq_mask = shift_in(G.irq_mask, p, sz, v); irq_update(); }
        else { uint32_t m = (sz == 4) ? 0xffffffffu : 0xffffffffu; (void)m; G.irq_stat = G.irq_stat & (shift_in(0xffffffffu, p, sz, v)) & G.irq_mask; irq_update(); }
        return;
    }
    if (p >= 0x1f801080 && p < 0x1f801100) {
        if (sz != 4) { log_once(p ^ 0x55, "[dma] sub-word write %08x size %d\n", p, sz); return; }
        dma_write((p - 0x1f801080) >> 2, v); return;
    }
    if (p >= 0x1f801100 && p < 0x1f801130) { if (sz == 4) rc_write((p - 0x1f801100) >> 2, v); else rc_write((p - 0x1f801100) >> 2, v & ((1u << (8 * sz)) - 1)); return; }
    if (p >= 0x1f801810 && p < 0x1f801818) { gpu_write32((p >> 2) & 1, v); return; }
    if (p == 0x1f801060) { G.ram_config = shift_in(G.ram_config, p, sz, v); fast_mem_update(); return; }
    if (p == 0x1f801010) { G.rom_config = shift_in(G.rom_config, p, sz, v); return; }
    if (p == 0x1f801000) { G.exp_base = shift_in(G.exp_base, p, sz, v); return; }
    if (p >= 0x1f801040 && p < 0x1f801060) { log_once(p ^ 0x77, "[io] SIO write %08x = %x (stub)\n", p, v); return; }
    if (p >= 0x1f801820 && p < 0x1f801828) { if (sz == 4) mdec_write((p >> 2) & 1, v); return; }
    if (p >= 0x1f801c00 && p < 0x1f802000 && G.sys10) { spu_write(p, sz, v); return; }
    if (p >= 0x1f801800 && p < 0x1f802000) return;
    if (p - 0x1f801000 < sizeof G.hwram) { wr_le(&G.hwram[p - 0x1f801000], sz, v); return; }
    log_once(p ^ 1, "[io] unmapped write %08x = %x size %d\n", p, v, sz);
}

/* debug: which code blocks read the gun words of the shared RAM (offsets 0x3400 = Y, 0x3402 = X) */
static uint32_t ring_last(void);
static uint32_t gun_pcs[24]; static int n_gun_pcs; static uint64_t gun_reads;
static void gun_note(uint32_t p, int sz)
{
    uint32_t o = p - 0x1f080000u; if (o + (uint32_t)sz <= 0x3400u || o >= 0x3404u) return;
    gun_reads++; uint32_t pc = ring_last();
    for (int i = 0; i < n_gun_pcs; i++) if (gun_pcs[i] == pc) return;
    if (n_gun_pcs < 24) gun_pcs[n_gun_pcs++] = pc;
}
static uint32_t bus_read(uint32_t a, int sz)
{
    if (a >= 0xc0000000u) {
        if (a == 0xfffe0130u) return shift_out(G.biu, a, sz);
        log_once(a, "[bus] kseg2 read %08x\n", a); return 0;
    }
    uint32_t p = a & 0x1fffffffu;
    uint32_t win = ram_window();
    if (p < win) { uint32_t rs = win < RAM_BYTES ? win : RAM_BYTES; return rd_le(&G.ram[p % rs], sz); }
    if (G.sys10 && p >= 0x1f000000u && p < 0x1fc00000u && !(p >= 0x1f800000u && p < 0x1f803000u)) { uint32_t sv; if (sys10_read(p, sz, &sv)) return sv; }
    if (p < 0x1f000000u) { log_once(p ^ 0x2, "[bus] read outside RAM window %08x\n", p); return 0; }
    if (p >= 0x1fc00000u) return rd_le(&G.rom_main[p - 0x1fc00000u], sz);
    if (p >= 0x1fa00000u && p < 0x1fc00000u) {
        uint64_t o = (uint64_t)G.bankoffset * 0x200000u + (p - 0x1fa00000u);
        if (o + (uint64_t)sz > 0x3c00000u) { log_once(0xba2c0000u + G.bankoffset, "[bank] read past ROM (page %u)\n", G.bankoffset); return 0; }
        if (gpu_trace_loads) {                                   /* coalesce sequential CPU reads of the banked ROM into runs */
            static uint64_t rs = ~0ull, re; static uint64_t rf;
            if (o != re || rs == ~0ull) {
                if (rs != ~0ull) fprintf(stderr, "BANKRD f=%llu abs=%08llx len=%llu\n", (unsigned long long)rf, (unsigned long long)rs, (unsigned long long)(re - rs));
                rs = o; rf = gpu_frame_no;
            }
            re = o + (uint64_t)sz;
        }
        return rd_le(&G.rom_bank[o], sz);
    }
    if (p >= 0x1f800000u && p < 0x1f800400u) return rd_le(&G.scratch[p - 0x1f800000u], sz);
    if (p >= 0x1f801000u && p < 0x1f803000u) return hw_read(p, sz);
    if (p >= 0x1f080000u && p < 0x1f084000u) { gun_note(p, sz); return rd_le(&G.sharedram[p - 0x1f080000u], sz); }
    if (p >= 0x1f140000u && p < 0x1f141000u) {            /* AT28C16 on byte lanes 0 and 2 (umask 0x00ff00ff) */
        uint32_t o = p - 0x1f140000u;
        if (sz == 4) return (uint32_t)G.eeprom[(o >> 1) & 0x7ff] | ((uint32_t)G.eeprom[((o >> 1) + 1) & 0x7ff] << 16);
        return (o & 1) ? 0 : G.eeprom[(o >> 1) & 0x7ff];
    }
    if (p >= 0x1f000000u && p < 0x1f000004u) return 0;
    log_once(p ^ 3, "[bus] unmapped read %08x size %d\n", p, sz);
    return 0;
}

static void bus_write(uint32_t a, int sz, uint32_t v)
{
    if (CPU.cop0[CP0_SR] & SR_ISC) return;           /* isolated cache: stores do not reach memory */
    if (a >= 0xc0000000u) {
        if (a == 0xfffe0130u) { G.biu = shift_in(G.biu, a, sz, v); return; }
        log_once(a ^ 4, "[bus] kseg2 write %08x\n", a); return;
    }
    uint32_t p = a & 0x1fffffffu;
    uint32_t win = ram_window();
    if (p < win) { uint32_t rs = win < RAM_BYTES ? win : RAM_BYTES; wr_le(&G.ram[p % rs], sz, v); return; }
    if (G.sys10 && p >= 0x1f000000u && p < 0x1fc00000u && !(p >= 0x1f800000u && p < 0x1f803000u)) { if (sys10_write(p, sz, v)) return; }
    if (p < 0x1f000000u) { log_once(p ^ 0x5, "[bus] write outside RAM window %08x\n", p); return; }
    if (p >= 0x1fc00000u) { log_once(p ^ 0x6, "[rom] write to ROM %08x ignored (first)\n", p); return; }
    if (p >= 0x1f800000u && p < 0x1f800400u) { wr_le(&G.scratch[p - 0x1f800000u], sz, v); return; }
    if (p >= 0x1f801000u && p < 0x1f803000u) { hw_write(p, sz, v); return; }
    if (p >= 0x1f080000u && p < 0x1f084000u) { wr_le(&G.sharedram[p - 0x1f080000u], sz, v); return; }
    if (p >= 0x1f140000u && p < 0x1f141000u) {
        uint32_t o = p - 0x1f140000u;
        if (sz == 4) { G.eeprom[(o >> 1) & 0x7ff] = (uint8_t)v; G.eeprom[((o >> 1) + 1) & 0x7ff] = (uint8_t)(v >> 16); }
        else if (!(o & 1)) G.eeprom[(o >> 1) & 0x7ff] = (uint8_t)v;
        return;
    }
    if (p == 0x1f000000u || p == 0x1f000001u || p == 0x1f000002u) {   /* bank offset (altbank), 16-bit register */
        uint32_t data = v & 0xffff;
        if (data & 8) G.bankoffset = (data - 8) << 2; else G.bankoffset = (G.bankoffset & ~7u) | (data & 7);
        return;
    }
    if (p >= 0x1f700000u && p < 0x1f710000u) { G.dmaoffset = (p & 0xffff) | ((v & 0xffff) << 16); return; }
    if (p == 0x1f008000u || p == 0x1f010000u || p == 0x1f018000u || (p >= 0x1f1bff08u && p < 0x1f1bff10u)) return;
    log_once(p ^ 7, "[bus] unmapped write %08x = %x size %d\n", p, v, sz);
}

/* ---- rt.h memory interface (generated code). Misaligned accesses abort: AdEL/AdES are not modelled. ---- */
static void misaligned(const char *what, uint32_t a)
{
    fprintf(stderr, "[fatal] misaligned %s at %08x\n", what, a); G.fatal = 1;
}
uint32_t rt_rd8_slow (cpu_t *c, uint32_t a) { (void)c; return bus_read(a, 1); }
uint32_t rt_rd16_slow(cpu_t *c, uint32_t a) { (void)c; if (a & 1) { misaligned("lh", a); return 0; } return bus_read(a, 2); }
uint32_t rt_rd32_slow(cpu_t *c, uint32_t a) { (void)c; if (a & 3) { misaligned("lw", a); return 0; } return bus_read(a, 4); }
void rt_wr8_slow (cpu_t *c, uint32_t a, uint32_t v) { (void)c; bus_write(a, 1, v & 0xff); }
void rt_wr16_slow(cpu_t *c, uint32_t a, uint32_t v) { (void)c; if (a & 1) { misaligned("sh", a); return; } bus_write(a, 2, v & 0xffff); }
void rt_wr32_slow(cpu_t *c, uint32_t a, uint32_t v) { (void)c; if (a & 3) { misaligned("sw", a); return; } bus_write(a, 4, v); }

/* fast-path state used by the inlined RAM accesses in rt.h (RT_INLINE_MEM) */
uint8_t *rt_ram; uint32_t rt_fast_limit, rt_fast_mask; int rt_isc;
static void fast_mem_update(void)
{
    uint32_t win = ram_window(), rs = win < RAM_BYTES ? win : RAM_BYTES;
    rt_ram = G.ram; rt_fast_limit = win; rt_fast_mask = rs - 1; rt_isc = (CPU.cop0[CP0_SR] & SR_ISC) != 0;
}

/* ------------------------------------------------------------------ COP0 / exceptions */
static struct { uint32_t kind, val, blk; uint64_t cyc; } sr_ev[48]; static int sr_ev_n;
static uint32_t ring_last(void);
static void sr_log(uint32_t kind, uint32_t val) { sr_ev[sr_ev_n % 48].kind = kind; sr_ev[sr_ev_n % 48].val = val; sr_ev[sr_ev_n % 48].blk = ring_last(); sr_ev[sr_ev_n % 48].cyc = CPU.cycles; sr_ev_n++; }

uint32_t rt_cop0_read(cpu_t *c, uint32_t n)
{
    if (n == 15) return 0x00000002u;                       /* PRID: R3000A */
    return c->cop0[n & 31];
}
void rt_cop0_write(cpu_t *c, uint32_t n, uint32_t v)
{
    n &= 31;
    switch (n) {
    case CP0_CAUSE: c->cop0[n] = (c->cop0[n] & ~0x300u) | (v & 0x300u); break;   /* only software interrupt bits writable */
    case CP0_EPC: case CP0_BADVADDR: case 15: break;
    case CP0_SR: sr_log(1, v); c->cop0[n] = v; fast_mem_update(); break;
    default: c->cop0[n] = v; break;
    }
}
void rt_rfe(cpu_t *c) { sr_log(2, c->cop0[CP0_SR]); uint32_t sr = c->cop0[CP0_SR]; c->cop0[CP0_SR] = (sr & ~0xfu) | ((sr >> 2) & 0xfu); }

uint32_t rt_exception(cpu_t *c, uint32_t code, uint32_t pc, uint32_t in_slot)
{
    sr_log(0x100 + code, pc);
    c->cop0[CP0_EPC] = pc;
    if (in_slot) c->cop0[CP0_CAUSE] |= 0x80000000u; else c->cop0[CP0_CAUSE] &= ~0x80000000u;
    c->cop0[CP0_CAUSE] = (c->cop0[CP0_CAUSE] & ~0x7cu) | (code << 2);
    uint32_t sr = c->cop0[CP0_SR];
    c->cop0[CP0_SR] = (sr & ~0x3fu) | ((sr << 2) & 0x3fu);
    return (sr & SR_BEV) ? 0xbfc00180u : 0x80000080u;
}

/* ------------------------------------------------------------------ unknown targets: report, never emulate */
static uint32_t ring[64]; static int ring_n;
static uint32_t ring_last(void) { return ring[(ring_n - 1) & 63]; }
uint32_t rt_unknown_pc(cpu_t *c, uint32_t pc)
{
    fprintf(stderr, "[fatal] jump to unknown code address %08x (cycle %llu, frame %llu)\n", pc,
            (unsigned long long)c->cycles, (unsigned long long)G.frame);
    fprintf(stderr, "  regs: ");
    for (int i = 0; i < 32; i++) fprintf(stderr, "%s%08x%s", (i % 8 == 0 && i) ? "\n        " : "", c->r[i], i % 8 == 7 ? "" : " ");
    fprintf(stderr, "\n  last blocks:");
    for (int i = 0; i < 16; i++) fprintf(stderr, " %08x", ring[(ring_n - 16 + i) & 63]);
    fprintf(stderr, "\n");
    G.fatal = 1;
    return 0;
}

/* ------------------------------------------------------------------ run loop */
void machine_init(void)
{
    memset(&G, 0, sizeof G); memset(&CPU, 0, sizeof CPU);
    G.ram = calloc(1, RAM_BYTES);
    memset(G.eeprom, 0xff, sizeof G.eeprom);
}
void machine_reset(void)
{
    memset(G.ram, 0, RAM_BYTES);
    G.irq_stat = G.irq_mask = 0; G.dpcr = G.dicr = 0; G.bankoffset = 0; G.dmaoffset = 0;
    G.ram_config = 0; G.rom_config = 0; G.exp_base = 0x1f000000u; G.biu = 0;
    gpu_reset(); mdec_reset();
    memset(CPU.r, 0, sizeof CPU.r); memset(CPU.cop0, 0, sizeof CPU.cop0);
    CPU.cop0[CP0_SR] = SR_BEV; CPU.cycles = 0; G.frame = 0; fast_mem_update();
    for (int i = 0; i < 3; i++) memset(&G.rc[i], 0, sizeof G.rc[i]);
    G.next_vblank = frame_cycles(); G.vb_end = 0; G.in_vblank = 0; G.h8_sync = 0;
    memset(G.sharedram, 0, sizeof G.sharedram);
    if (G.sys10) { G.prof_noh8 = 1; G.prof_noaudio = 1; sys10_reset(); spu_init(spu_irq9); sys10_next_io = CPU_HZ / 100; return; }
    HM.rom = G.rom_sub; (void)c352_store; HM.sharedram = G.sharedram; memset(HM.ram, 0, sizeof HM.ram);
    memset(HM.in, 0xff, sizeof HM.in); HM.c352_regs = c352_store; HM.dsw = 0xff;
    for (int i = 0; i < 4; i++) HM.adc_in[i] = 0;
    HM.portb_out = 0;
    h8m_reset();
    c352_init(G.rom_wave, 0x1000000);
    /* boot hack (namcos12.cpp machine_reset, boot_hack=true for coh700b): 12 bytes of the main ROM are copied into RAM */
    memcpy(G.ram + 0x10000, G.rom_main + 0x20280, 12);
}

static void audio_flush(void)
{
    if (G.prof_noaudio) return;
    static int16_t buf[8192 * 2];
    c352_catchup(h8m_states_for_main_cycles(CPU.cycles));
    size_t n;
    while ((n = c352_take(buf, 8192)) > 0) { if (G.audio_sink) G.audio_sink(buf, n); }
}

static void service_events(void)
{
    int new_frame = 0;
    while (CPU.cycles >= G.next_vblank) {
        G.frame++; new_frame = 1;
        gpu_vblank();
        gpu_log = (G.log_frame && (G.log_frame == 99999999u || G.frame == G.log_frame));
        if (G.shot_path && G.frame == G.shot_frame) machine_save_shot(G.shot_path);
        irq_set(1u << 0);
        if (!G.sys10) h8m_vblank(1); G.in_vblank = 1; G.vb_end = G.next_vblank + frame_cycles() * 22 / 262;
        G.next_vblank += frame_cycles();
        if (G.max_frames && G.frame >= G.max_frames) G.stop = 1;
    }
    if (G.in_vblank && CPU.cycles >= G.vb_end) { if (!G.sys10) h8m_vblank(0); G.in_vblank = 0; }
    if (G.sys10 && CPU.cycles >= sys10_next_io) { sys10_tick(); irq_set(1u << 10); sys10_next_io = CPU.cycles + CPU_HZ / 100; }
    if (!G.sys10 && CPU.cycles >= G.h8_sync) {
        if (!G.prof_noh8) h8m_run(h8m_states_for_main_cycles(CPU.cycles)); G.h8_sync = CPU.cycles + H8_SYNC_CYCLES;
        if (h8m_fatal()) G.fatal = 1;
    }
    if (new_frame && !G.sys10) audio_flush();
    if (new_frame && G.sys10 && G.audio_sink) {   /* System 10: SPU at 44100 Hz, duplicated to the 88200 Hz output stream */
        static int16_t mixb[735 * 2], outb[1470 * 2]; spu_mix(mixb, 735); mp3_mix(mixb, 735);
        for (int k = 0; k < 735; k++) { outb[k * 4] = outb[k * 4 + 2] = mixb[k * 2]; outb[k * 4 + 1] = outb[k * 4 + 3] = mixb[k * 2 + 1]; }
        G.audio_sink(outb, 1470);
    }
    for (int n = 0; n < 3; n++) if (G.rc[n].armed && CPU.cycles >= G.rc[n].due) rc_fire(n);
    uint64_t nx = G.next_vblank;
    if (G.in_vblank && G.vb_end < nx) nx = G.vb_end;
    if (!G.sys10 && G.h8_sync < nx) nx = G.h8_sync;
    if (G.sys10 && sys10_next_io < nx) nx = sys10_next_io;
    for (int n = 0; n < 3; n++) if (G.rc[n].armed && G.rc[n].due < nx) nx = G.rc[n].due;
    G.next_event = nx;
}

static uint32_t watch[8]; static int nwatch; static uint64_t w_first[8], w_last[8], w_count[8];
void machine_watch(uint32_t pc) { if (nwatch < 8) watch[nwatch++] = pc; }
static uint32_t run_pc = 0xbfc00000u;
/* flat dispatch tables (filled from the generated per-image tables): RAM images and the BIOS ROM */
#define RAM_TBL_WORDS 0x100000u
#define ROM_TBL_WORDS 0xa000u
static blk_fn *ram_tbl, *rom_tbl;
static inline blk_fn lookup_block(uint32_t pc)
{
    uint32_t p = pc & 0x1fffffffu;
    if (p < RAM_TBL_WORDS * 4u) return ram_tbl[p >> 2];
    p -= 0x1fc00000u;
    return p < ROM_TBL_WORDS * 4u ? rom_tbl[p >> 2] : 0;
}
static uint32_t wwatch_addr, wwatch_val; static int trace_left = -1; static uint64_t trace_from; static int wwatch_on = -1;   /* debug: G13_WWATCH=<hex address> logs every block that changes that word */
static void run_loop(uint64_t until_frame)
{
    if (trace_left < 0) { const char *e = getenv("G13_TRACE"); trace_left = e ? atoi(e) : 0; const char *f = getenv("G13_TRACEF"); trace_from = f ? (uint64_t)atoll(f) : 0; }
    if (wwatch_on < 0) { const char *e = getenv("G13_WWATCH"); wwatch_on = e ? 1 : 0; if (e) wwatch_addr = (uint32_t)strtoul(e, NULL, 16); }
    uint32_t pc = run_pc;
    if (!ram_tbl) {
        ram_tbl = (blk_fn *)calloc(RAM_TBL_WORDS, sizeof(blk_fn)); rom_tbl = (blk_fn *)calloc(ROM_TBL_WORDS, sizeof(blk_fn));
        rt_code_export_all(ram_tbl, RAM_TBL_WORDS, rom_tbl, ROM_TBL_WORDS);
    }
    while (!G.stop && !G.fatal && (!until_frame || G.frame < until_frame)) {
        blk_fn f = lookup_block(pc);
        if (!f) { pc = rt_unknown_pc(&CPU, pc); continue; }
        ring[ring_n++ & 63] = pc;
        if (nwatch) for (int w = 0; w < nwatch; w++) if (pc == watch[w]) { if (!w_count[w]) w_first[w] = G.frame; w_last[w] = G.frame; w_count[w]++; }
        uint32_t blk_pc = pc;
        if (trace_left > 0 && G.frame >= trace_from) { trace_left--; fprintf(stderr, "tr %08x\n", pc); }
        pc = f(&CPU); G.nblocks++;
        if (wwatch_on > 0) { uint32_t nv = rt_rd32_slow(&CPU, wwatch_addr); if (nv != wwatch_val) { fprintf(stderr, "wwatch %08x: %08x -> %08x by block %08x (frame %llu)\n", wwatch_addr, wwatch_val, nv, blk_pc, (unsigned long long)G.frame); wwatch_val = nv; if (getenv("G13_WRING")) { fprintf(stderr, "  ring:"); for (int k = 40; k >= 1; k--) fprintf(stderr, " %08x", ring[(ring_n - k) & 63]); fputc(10, stderr); } } }
        if (CPU.cycles >= G.next_event) service_events();
        uint32_t sr = CPU.cop0[CP0_SR];
        if ((sr & SR_IEC) && (CPU.cop0[CP0_CAUSE] & sr & 0xff00u)) pc = rt_exception(&CPU, EXC_INT, pc, 0);
    }
    run_pc = pc;
}
uint32_t machine_peek32(uint32_t addr) { return rt_rd32_slow(&CPU, addr); }
void machine_run(void) { run_pc = 0xbfc00000u; service_events(); run_loop(0); }
static uint32_t g_fc_addr[4]; static int g_fc_n; static int g_fc_val = 9;
void machine_set_free_credit(const uint32_t *addr, int n, int value) { g_fc_n = n > 4 ? 4 : n; for (int i = 0; i < g_fc_n; i++) g_fc_addr[i] = addr[i]; g_fc_val = value; }
int machine_run_frames(int n)               /* advance the game by n video frames; 0 = ok, 1 = stopped (fatal) */
{
    static int started;
    if (!started) { started = 1; run_pc = 0xbfc00000u; service_events(); }
    run_loop(G.frame + (uint64_t)n);
    for (int i = 0; i < g_fc_n; i++) machine_poke8(g_fc_addr[i], (uint8_t)g_fc_val);        /* credit counter of the game: always full */
    return G.fatal;
}

static inline int bitn(uint32_t v, int n) { return (int)((v >> n) & 1); }
void machine_set_input(int gun_x, int gun_y, int trigger, int sensor, int enter, int up, int down, int start, int coin, int service, int test)
{
    uint32_t p1 = (start ? 0x80u : 0) | (service ? 0x40u : 0) | (up ? 0x20u : 0) | (down ? 0x10u : 0) | (enter ? 0x02u : 0) | (trigger ? 0x01u : 0) | (sensor ? 0x800u : 0);
    uint32_t p2 = 0, sys = test ? 0x80u : 0; int c1 = coin != 0, c2 = 0;
    if (G.sys10) { sys10_set_inputs(trigger, sensor, enter, up, down, start, coin); sys10_set_gun(gun_x, gun_y); return; }
    #define SW(p) ((bitn(p,7)<<7)|(bitn(p,15)<<6)|(bitn(p,0)<<5)|(bitn(p,1)<<4)|(bitn(p,5)<<3)|(bitn(p,4)<<2)|(bitn(p,3)<<1)|bitn(p,2))
    HM.in[0] = (uint8_t)~SW(p2); HM.in[1] = (uint8_t)~SW(p1);
    HM.in[2] = (uint8_t)~((bitn(p1,6)<<7)|(bitn(sys,7)<<6)|(c1<<5)|(c2<<4)|(bitn(p1,11)<<3)|(bitn(p2,11)<<2)|(bitn(p1,10)<<1)|bitn(p2,10));
    HM.in[3] = (uint8_t)~((((bitn(p2,12)<<2)|(bitn(p2,13)<<1)|bitn(p2,14))<<5)|(((bitn(p1,12)<<2)|(bitn(p1,13)<<1)|bitn(p1,14))<<1));
    /* Gun ADC model matching the game's own factory calibration (see PLAN/aim notes): the game calibrates on screen points
       1/8 inset (x 64/256/448, y 60/240/420) <-> raw counts X 224/416/608 (1.000 count/px, count = x+160) and Y 96/256/416
       (0.889 count/px, raw decreasing downward). We feed exactly that characteristic so the default calibration is exact. */
    { int xpx = gun_x, ypx = (gun_y * 479 + 255) / 511;
      HM.adc_in[1] = (uint16_t)(xpx + 160);
      HM.adc_in[2] = (uint16_t)(256 - ((ypx - 240) * 8) / 9 + 0); }
}

void machine_dump_state(void)
{
    if (gun_reads) { fprintf(stderr, "gun words read %llu times by blocks:", (unsigned long long)gun_reads); for (int i = 0; i < n_gun_pcs; i++) fprintf(stderr, " %08x", gun_pcs[i]); fprintf(stderr, "\n"); }
    for (int w = 0; w < nwatch; w++) fprintf(stderr, "watch %08x: first frame %llu, last frame %llu, hits %llu\n", watch[w], (unsigned long long)w_first[w], (unsigned long long)w_last[w], (unsigned long long)w_count[w]);
    fprintf(stderr, "SR events (kind: 1=mtc0 SR, 2=rfe, 0x1xx=exception code xx) newest last:\n");
    for (int i = 24; i > 0; i--) { int k = (sr_ev_n - i); if (k < 0) continue; fprintf(stderr, "  [%llu] kind=%x val=%08x in block %08x\n", (unsigned long long)sr_ev[k % 48].cyc, sr_ev[k % 48].kind, sr_ev[k % 48].val, sr_ev[k % 48].blk); }
    fprintf(stderr, "MIPS: SR=%08x CAUSE=%08x EPC=%08x irq_stat=%08x irq_mask=%08x gpustat=%08x\n", CPU.cop0[CP0_SR], CPU.cop0[CP0_CAUSE], CPU.cop0[CP0_EPC], G.irq_stat, G.irq_mask, gpu_status(G.frame, 0));
    fprintf(stderr, "regs:"); for (int i = 1; i < 32; i++) fprintf(stderr, " r%d=%08x", i, CPU.r[i]); fputc(10, stderr);
    fprintf(stderr, "last blocks:");
    for (int i = 0; i < 24; i++) fprintf(stderr, " %08x", ring[(ring_n - 24 + i) & 63]);
    fprintf(stderr, "\n");
}

void machine_save_shot(const char *path)
{
    static uint8_t buf[1024 * 512 * 4]; int w, h;
    gpu_log = 1; gpu_get_display(buf, &w, &h); gpu_log = 0;
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6 %d %d 255", w, h); fputc(10, f);
    for (int i = 0; i < w * h; i++) fwrite(&buf[i * 4], 1, 3, f);
    fclose(f);
}

void machine_poke8(uint32_t addr, uint8_t v) { uint32_t o = addr & 0x1fffffffu; if (o < RAM_BYTES) G.ram[o] = v; }
void machine_dump_ram(const char *path)
{
    FILE *f = fopen(path, "wb"); if (!f) return; fwrite(G.ram, 1, RAM_BYTES, f); fclose(f);
}
