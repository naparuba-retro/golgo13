/* PlayStation SPU, bus side only (System 10): registers, 512 KB sound RAM, manual and DMA transfers, IRQ9 on the programmed address.
 * Voice synthesis / reverb are not implemented yet (the audio output stays silent). Layout after psx-spx and MAME src/devices/sound/spu.cpp. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spu.h"

#define SPU_RAM_BYTES 0x80000u
static uint8_t  ram[SPU_RAM_BYTES];
static uint16_t reg[0x100];                 /* 0x1f801c00 + 2*i */
static uint32_t xfer_addr;                  /* current transfer address in bytes */
static int      irq_pending;
static void   (*irq_cb)(void);

enum { R_IRQADDR = 0x1a4 / 2, R_XADDR = 0x1a6 / 2, R_XFIFO = 0x1a8 / 2, R_CNT = 0x1aa / 2, R_XCTRL = 0x1ac / 2, R_STAT = 0x1ae / 2 };


static void check_irq(uint32_t addr_bytes)
{
    if (!(reg[R_CNT] & 0x40)) return;
    if ((addr_bytes & ~7u) == ((uint32_t)reg[R_IRQADDR] * 8u)) {
        reg[R_STAT] |= 0x40; if (irq_cb) irq_cb();
    }
}

static void put_word(uint16_t w)
{
    check_irq(xfer_addr);
    if (xfer_addr + 1 < SPU_RAM_BYTES) { ram[xfer_addr] = (uint8_t)w; ram[xfer_addr + 1] = (uint8_t)(w >> 8); }
    xfer_addr = (xfer_addr + 2) & (SPU_RAM_BYTES - 1);
}

/* ---------------------------------------------------------------- voices (ADPCM, ADSR, mixing) */
typedef struct { int on, phase; uint32_t addr, repeat; int16_t s[28]; int idx; uint32_t cnt; int16_t h1, h2; int32_t level; uint32_t acnt; int end; } voice_t;
enum { PH_ATTACK, PH_DECAY, PH_SUSTAIN, PH_RELEASE };
static voice_t vc[24]; static uint32_t endx;
static const int FILT[5][2] = { { 0, 0 }, { 60, 0 }, { 115, -52 }, { 98, -55 }, { 122, -60 } };

static void decode_block(voice_t *v)
{
    const uint8_t *b = &ram[v->addr & (SPU_RAM_BYTES - 1)]; int shift = b[0] & 15, filt = (b[0] >> 4) & 7; if (filt > 4) filt = 4; if (shift > 12) shift = 9;
    int f0 = FILT[filt][0], f1 = FILT[filt][1]; int32_t h1 = v->h1, h2 = v->h2;
    for (int i = 0; i < 28; i++) {
        int nib = (b[2 + i / 2] >> ((i & 1) * 4)) & 15; int32_t smp = (int16_t)(nib << 12); smp >>= shift;
        smp += (h1 * f0 + h2 * f1 + 32) >> 6; if (smp > 32767) smp = 32767; if (smp < -32768) smp = -32768;
        v->s[i] = (int16_t)smp; h2 = h1; h1 = smp;
    }
    v->h1 = (int16_t)h1; v->h2 = (int16_t)h2;
}

static void kon_mask(uint32_t m, int base)
{
    for (int i = 0; i < 16; i++) if ((m >> i) & 1) {
        int n = base + i; if (n >= 24) continue; voice_t *v = &vc[n]; const uint16_t *r = &reg[n * 8];
        v->on = 1; v->phase = PH_ATTACK; v->addr = (uint32_t)r[3] * 8u; v->repeat = (uint32_t)r[7] * 8u; v->level = 0; v->acnt = 0; v->cnt = 0; v->idx = 0; v->h1 = v->h2 = 0; v->end = 0;
        endx &= ~(1u << n); decode_block(v);
    }
}
static void koff_mask(uint32_t m, int base) { for (int i = 0; i < 16; i++) if (((m >> i) & 1) && base + i < 24) { voice_t *v = &vc[base + i]; if (v->on) v->phase = PH_RELEASE; } }

static void env_step(voice_t *v, const uint16_t *r)
{
    uint16_t lo = r[4], hi = r[5]; int exp, dec, shift, step;
    switch (v->phase) {
    case PH_ATTACK: exp = (lo >> 15) & 1; dec = 0; shift = (lo >> 10) & 31; step = 7 - ((lo >> 8) & 3); break;
    case PH_DECAY: exp = 1; dec = 1; shift = (lo >> 4) & 15; step = -8; break;
    case PH_SUSTAIN: exp = (hi >> 15) & 1; dec = (hi >> 14) & 1; shift = (hi >> 8) & 31; step = 7 - ((hi >> 6) & 3); if (dec) step = -step; break;
    default: exp = (hi >> 5) & 1; dec = 1; shift = hi & 31; step = -8; break;
    }
    uint32_t cinc = shift >= 11 ? 0x8000u >> (shift - 11) : 0x8000u; int32_t st = step << (shift < 11 ? 11 - shift : 0);
    if (exp && !dec && v->level > 0x6000) cinc >>= 2;
    if (exp && dec) st = (st * v->level) >> 15;
    v->acnt += cinc; if (!(v->acnt & 0x8000)) goto check; v->acnt = 0;
    v->level += st; if (v->level < 0) v->level = 0; if (v->level > 0x7fff) v->level = 0x7fff;
check:
    if (v->phase == PH_ATTACK && v->level >= 0x7fff) v->phase = PH_DECAY;
    else if (v->phase == PH_DECAY && v->level <= (int32_t)(((lo & 15) + 1) * 0x800)) v->phase = PH_SUSTAIN;
    else if (v->phase == PH_RELEASE && v->level <= 0) v->on = 0;
}
static int32_t vol_of(uint16_t v) { return (int16_t)(v << 1); }
void spu_init(void (*irq)(void)) { memset(vc, 0, sizeof vc); endx = 0; memset(ram, 0, sizeof ram); memset(reg, 0, sizeof reg); xfer_addr = 0; irq_pending = 0; irq_cb = irq; }

/* mixes n stereo samples (44100 Hz) into out (interleaved L,R int16) */
void spu_mix(int16_t *out, int n)
{
    int32_t mvl = vol_of(reg[0x180 / 2]), mvr = vol_of(reg[0x182 / 2]); int enabled = (reg[R_CNT] & 0x8000) != 0;
    for (int k = 0; k < n; k++) {
        int32_t l = 0, r = 0;
        for (int i = 0; i < 24 && enabled; i++) {
            voice_t *v = &vc[i]; if (!v->on) continue; const uint16_t *rg = &reg[i * 8];
            uint32_t pitch = rg[2]; if (pitch > 0x3fff) pitch = 0x3fff;
            int32_t a = v->s[v->idx], b2 = v->idx + 1 < 28 ? v->s[v->idx + 1] : a;
            int32_t smp = a + (((b2 - a) * (int32_t)(v->cnt & 0xfff)) >> 12);
            smp = (smp * v->level) >> 15;
            l += (smp * vol_of(rg[0])) >> 15; r += (smp * vol_of(rg[1])) >> 15;
            env_step(v, rg);
            v->cnt += pitch; int adv = (int)(v->cnt >> 12); v->cnt &= 0xfff;
            v->idx += adv;
            while (v->idx >= 28) {
                v->idx -= 28; uint8_t fl = ram[(v->addr + 1) & (SPU_RAM_BYTES - 1)];
                if (fl & 4) v->repeat = v->addr;
                if (fl & 1) { endx |= 1u << i; if (fl & 2) v->addr = v->repeat; else { v->on = 0; break; } } else v->addr = (v->addr + 16) & (SPU_RAM_BYTES - 1);
                decode_block(v);
            }
        }
        l = (l * mvl) >> 15; r = (r * mvr) >> 15;
        if (l > 32767) l = 32767; if (l < -32768) l = -32768; if (r > 32767) r = 32767; if (r < -32768) r = -32768;
        out[k * 2] = (int16_t)l; out[k * 2 + 1] = (int16_t)r;
    }
}

uint32_t spu_read(uint32_t p, int sz)
{
    uint32_t i = (p - 0x1f801c00u) >> 1; if (i >= 0x100) return 0;
    uint32_t v;
    if (i == R_STAT) { v = (reg[R_CNT] & 0x3f) | (reg[R_STAT] & 0x40); /* IRQ flag; transfer is instantaneous so never busy */ }
    else if (i == R_XADDR) v = reg[R_XADDR];
    else if (i == 0x19c / 2) v = endx & 0xffff;
    else if (i == 0x19e / 2) v = (endx >> 16) & 0xff;
    else v = reg[i];
    if (sz == 1) return (p & 1) ? (v >> 8) & 0xff : v & 0xff;
    if (sz == 4) { uint32_t hi = (i + 1) < 0x100 ? reg[i + 1] : 0; return v | (hi << 16); }
    return v & 0xffff;
}

void spu_write(uint32_t p, int sz, uint32_t v)
{
    uint32_t i = (p - 0x1f801c00u) >> 1; if (i >= 0x100) return;
    if (sz == 4) { spu_write(p, 2, v & 0xffff); spu_write(p + 2, 2, v >> 16); return; }
    if (sz == 1) { uint16_t old = reg[i]; v = (p & 1) ? ((old & 0xff) | (v << 8)) : ((old & 0xff00) | (v & 0xff)); }
    v &= 0xffff;
    switch (i) {
    case R_XADDR: reg[i] = (uint16_t)v; xfer_addr = (v * 8u) & (SPU_RAM_BYTES - 1); return;
    case R_XFIFO: put_word((uint16_t)v); return;                                 /* manual write */
    case R_CNT: reg[i] = (uint16_t)v; if (!(v & 0x40)) reg[R_STAT] &= ~0x40; return;   /* clearing the IRQ enable acknowledges the IRQ flag */
    case R_STAT: return;
    case 0x188 / 2: reg[i] = (uint16_t)v; kon_mask(v, 0); return;
    case 0x18a / 2: reg[i] = (uint16_t)v; kon_mask(v, 16); return;
    case 0x18c / 2: reg[i] = (uint16_t)v; koff_mask(v, 0); return;
    case 0x18e / 2: reg[i] = (uint16_t)v; koff_mask(v, 16); return;
    default: reg[i] = (uint16_t)v; return;
    }
}

uint32_t spu_dma_addr(void) { return xfer_addr; }

void spu_dma_write(const uint32_t *words, int32_t nwords)            /* RAM -> SPU RAM (channel 4) */
{
    for (int32_t i = 0; i < nwords; i++) { put_word((uint16_t)words[i]); put_word((uint16_t)(words[i] >> 16)); }
    /* the transfer is complete: signal an IRQ if the transfer ended on / passed the IRQ address was already handled in put_word */
}

void spu_dma_read(uint32_t *words, int32_t nwords)                    /* SPU RAM -> RAM */
{
    for (int32_t i = 0; i < nwords; i++) {
        uint32_t a = xfer_addr & (SPU_RAM_BYTES - 1);
        words[i] = (uint32_t)ram[a] | ((uint32_t)ram[a + 1] << 8) | ((uint32_t)ram[a + 2] << 16) | ((uint32_t)ram[a + 3] << 24);
        xfer_addr = (xfer_addr + 4) & (SPU_RAM_BYTES - 1);
    }
}
