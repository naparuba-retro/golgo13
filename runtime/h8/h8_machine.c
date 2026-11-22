/* H8/3002 sub-CPU machine (Namco System 12 I/O + sound processor): bus map and on-chip peripherals.
 * Peripheral behaviour re-expressed in C from MAME src/devices/cpu/h8/{h8_intc,h8_timer16,h8_sci,h8_adc,h8_port}.cpp
 * (license:BSD-3-Clause, copyright-holders:Olivier Galibert) and the board glue of src/mame/namco/namcos12.cpp.
 * The CPU itself is the statically recompiled firmware (generated_h8/); there is no instruction interpreter. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "h8_machine.h"
#include "h8_alu.h"
#include "machine.h"
#include "c352.h"

#define H8_HZ 16934400ull

h8_t H8;
h8m_t HM;

static uint32_t pc;          /* guest PC between blocks */
static uint32_t hring[64]; static int hring_n;
static int h8_fatal;

/* ----------------------------------------------------------------- helpers */
static uint8_t  regs[0x100]; /* generic on-chip register file 0xffff00..0xffffff for registers without a model */
static void logh(uint32_t key, const char *fmt, ...);
#include <stdarg.h>
static void logh(uint32_t key, const char *fmt, ...)
{
    static uint32_t seen[1024]; static int n;
    for (int i = 0; i < n; i++) if (seen[i] == key) return;
    if (n < 1024) seen[n++] = key;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

/* ----------------------------------------------------------------- INTC (h8h_intc) */
enum { VEC_IRQ0 = 12, VEC_NMI = 7 };
static uint32_t pending[2];          /* vectors 0..63 */
static uint8_t  ier, isr, iscr, irq_input, irq_type_edge; /* irq_type_edge bit i = edge (iscr bit) */
static uint32_t icr;                 /* 24-bit register, only low 16 used */
static uint8_t  syscr = 0x09;
static const int8_t vector_to_slot[64] = {
    -1, -1, -1, -1, -1, -1, -1, -1,  -1, -1, -1, -1,  0,  1,  2,  2,
     3,  3,  3,  3,  4,  4,  4,  4,   5,  5,  5,  5,  6,  6,  6,  6,
     7,  7,  7,  7,  8,  8,  8,  8,   9,  9,  9,  9, 10, 10, 10, 10,
    11, 11, 11, 11, 12, 12, 12, 12,  13, 13, 13, 13, 14, 14, 14, 14 };
static void int_set(int v) { pending[v >> 5] |= 1u << (v & 31); }
static void int_clear(int v) { pending[v >> 5] &= ~(1u << (v & 31)); }
static void update_irq_types(void) { irq_type_edge = iscr; }
static void check_level_irqs(void)
{
    for (int i = 0; i < 8; i++) { uint8_t m = 1u << i; if (!(irq_type_edge & m) && (irq_input & m) && !(isr & m)) isr |= m; }
}
static void irq_input_set(int n, int state)       /* state 1 = ASSERT_LINE */
{
    uint8_t m = 1u << n, cur = irq_input & m; int set = 0;
    if (!(irq_type_edge & m)) set = state;          /* level low (sensed as asserted) */
    else set = state && !cur;                       /* edge: MAME EDGE_FALL: ASSERT with previous clear */
    if (state) irq_input |= m; else irq_input &= ~m;
    if (set) isr |= m;
}
static int get_icr_pri(int vect)
{
    if (vect == VEC_NMI) return 2;
    int slot = vector_to_slot[vect];
    if (slot < 0) return 0;
    return (icr >> (slot ^ 7)) & 1;
}
/* returns vector to take or 0; sets *level */
static int pick_interrupt(void)
{
    int filter;
    if ((syscr & 0x08) == 0) filter = ((H8.ccr & (F_I | F_UI)) == (F_I | F_UI)) ? 2 : (H8.ccr & F_I) ? 1 : 0;
    else filter = (H8.ccr & F_I) ? 2 : 0;
    uint32_t p0 = pending[0] & ~(0xffu << VEC_IRQ0), p1 = pending[1];
    p0 |= (uint32_t)(isr & ier) << VEC_IRQ0;
    int best = 0, bestlvl = -1;
    for (int v = 1; v < 64; v++) {
        uint32_t pend = v < 32 ? p0 : p1;
        if (!(pend & (1u << (v & 31)))) continue;
        int pri = get_icr_pri(v);
        if (pri >= filter && pri > bestlvl) { best = v; bestlvl = pri; }
    }
    return best;
}

/* ----------------------------------------------------------------- ITU (5 x 16-bit timers) */
typedef struct { uint8_t tcr, tier, tsr; uint16_t tcnt, tgr[2], tbr[2]; int enabled; uint64_t acc; int base_vec; } itu_t;
static itu_t itu[5]; static uint8_t tstr = 0xe0, tmdr, tfcr, toer, tocr, tsyr;
static int itu_div(const itu_t *t) { int s = t->tcr & 7; return s < 4 ? s : -1; }   /* phi, /2, /4, /8 ; external clocks not modelled */
static void itu_update_ints(itu_t *t)
{
    for (int i = 0; i < 3; i++) {
        if ((t->tsr & (1u << i)) && (t->tier & (1u << i))) int_set(t->base_vec + i); else int_clear(t->base_vec + i);
    }
}
static void itu_advance(itu_t *t, uint64_t delta)
{
    if (!t->enabled) return;
    int d = itu_div(t); if (d < 0) return;
    t->acc += delta; uint64_t ticks = t->acc >> d; t->acc &= ((1ull << d) - 1);
    while (ticks) {
        int cclr = (t->tcr >> 5) & 3;       /* 1: GRA, 2: GRB (TCR bits 6-5) */
        uint32_t period = 0x10000; int clr_gr = -1;
        if (cclr == 1) clr_gr = 0; else if (cclr == 2) clr_gr = 1;
        if (clr_gr >= 0) period = (uint32_t)t->tgr[clr_gr] + 1;
        uint32_t cur = t->tcnt;
        uint32_t step = ticks;
        /* next compare match / overflow position strictly after cur */
        uint32_t nxt = 0x10000; int ev_gr[2] = {0, 0}, ev_ovf = 0;
        for (int g = 0; g < 2; g++) { uint32_t cmp = (uint32_t)t->tgr[g] + 1; if (cmp > cur && cmp < nxt) nxt = cmp; }
        if (period < nxt && period > cur) nxt = period;
        if (nxt > period && period > cur) nxt = period;
        uint32_t room = nxt - cur;
        if (step > room) step = room;
        t->tcnt = (uint16_t)(cur + step); ticks -= step;
        uint32_t now = cur + step;
        for (int g = 0; g < 2; g++) if (now == (uint32_t)t->tgr[g] + 1) ev_gr[g] = 1;
        if (now == 0x10000) ev_ovf = 1;
        if (clr_gr >= 0 && now == period) { t->tcnt = 0; }
        else if (now == 0x10000) t->tcnt = 0;
        for (int g = 0; g < 2; g++) if (ev_gr[g]) t->tsr |= 1u << g;
        if (ev_ovf) t->tsr |= 4;
        if (ev_gr[0] || ev_gr[1] || ev_ovf) itu_update_ints(t);
    }
}

/* ----------------------------------------------------------------- SCI (2 channels, TX timing only; no receive device) */
typedef struct { uint8_t smr, brr, scr, tdr, ssr, rdr, ssr_read; int tx_busy; uint64_t tx_end; int base_vec; } sci_t;
static sci_t sci[2];
enum { SSR_TDRE = 0x80, SSR_RDRF = 0x40, SSR_ORER = 0x20, SSR_FER = 0x10, SSR_PER = 0x08, SSR_TEND = 0x04, SSR_MPB = 0x02, SSR_MPBT = 0x01,
       SCR_TIE = 0x80, SCR_RIE = 0x40, SCR_TE = 0x20, SCR_RE = 0x10, SCR_TEIE = 0x04 };
static void sci_ints(sci_t *s)
{
    if ((s->scr & SCR_TIE) && (s->ssr & SSR_TDRE)) int_set(s->base_vec + 2); else int_clear(s->base_vec + 2);
    if ((s->scr & SCR_TEIE) && (s->ssr & SSR_TEND)) int_set(s->base_vec + 3); else int_clear(s->base_vec + 3);
    if ((s->scr & SCR_RIE) && (s->ssr & SSR_RDRF)) int_set(s->base_vec + 1); else int_clear(s->base_vec + 1);
}
static uint64_t sci_bit_states(const sci_t *s)
{
    int n = s->smr & 3; uint64_t div = ((s->smr & 0x80) ? 4ull : 32ull) << (2 * n);   /* clock sync : async */
    return div * (uint64_t)(s->brr + 1);
}
static void sci_tx_start(sci_t *s)
{
    /* load shifter from TDR: TDRE set again, TEND cleared */
    s->tx_busy = 1; s->ssr |= SSR_TDRE; s->ssr &= ~SSR_TEND;
    int bits = (s->smr & 0x80) ? 8 : 10;
    s->tx_end = H8.cycles + bits * sci_bit_states(s);
    sci_ints(s);
}
static void sci_advance(sci_t *s)
{
    if (s->tx_busy && H8.cycles >= s->tx_end) {
        s->tx_busy = 0;
        if (!(s->ssr & SSR_TDRE)) sci_tx_start(s);          /* next byte already queued */
        else { s->ssr |= SSR_TEND; sci_ints(s); }
    }
}

/* ----------------------------------------------------------------- ADC (H8_ADC_3337: 4 result registers, scan/single) */
static uint8_t adcsr, adcr; static uint16_t addr_[4], adc_buf; static int adc_active, adc_chan, adc_start_ch, adc_end_ch, adc_rotate, adc_repeat, adc_trg_ext;
static uint64_t adc_next; static int adtrg = 1;
static void adc_mode_update(void)
{
    adc_trg_ext = (adcr & 0x80) != 0;
    if (adcsr & 0x10) { adc_rotate = 1; adc_repeat = 1; adc_start_ch = adcsr & 4; adc_end_ch = adcsr & 7; }
    else { adc_rotate = 0; adc_repeat = 0; adc_start_ch = adc_end_ch = adcsr & 7; }
}
static uint64_t adc_conv_time(int first) { return first ? ((adcsr & 0x08) ? 134 : 266) : ((adcsr & 0x08) ? 128 : 256); }
static void adc_sample(void) { adc_buf = HM.adc_in[adc_chan & 3]; }
static void adc_start(void) { adc_active = 1; adc_chan = adc_start_ch; adc_sample(); adc_next = H8.cycles + adc_conv_time(1); }
static void adc_timeout(uint64_t due)
{
    addr_[adc_chan & 3] = adc_buf;
    if (adc_rotate) {
        if (adc_chan != adc_end_ch) { adc_chan = (adc_chan + 1) & 7; adc_sample(); adc_next = due + adc_conv_time(0); return; }
        adc_chan = adc_start_ch;
    }
    adcsr |= 0x80;
    if (adcsr & 0x40) int_set(60);
    if (adc_repeat) { adc_sample(); adc_next = due + adc_conv_time(0); return; }
    adc_active = 0; adcsr &= ~0x20; adc_next = 0;
}
static void adc_advance(void)
{
    while (adc_next && H8.cycles >= adc_next) { uint64_t due = adc_next; adc_next = 0; adc_timeout(due); }
}

/* ----------------------------------------------------------------- ports */
static uint8_t port_ddr[16], port_dr[16];
enum { P4 = 4, P6 = 6, P7 = 7, P8 = 8, P9 = 9, PA = 10, PB = 11 };
static const uint8_t port_mask[16] = { [P6] = 0x80, [P8] = 0xe0, [P9] = 0xc0 };
static uint8_t port_ext_read(int p)
{
    switch (p) {
    case P6: return 0xff;                     /* JVS sense: no JVS board -> bit1 set */
    case P7: return HM.dsw;
    case P8: return 0xef;                     /* JVS sense none -> bit4 clear */
    case PA: return 0xff;
    case PB: return (uint8_t)(HM.portb_out | 0x7f);
    default: return 0xff;
    }
}
static uint8_t port_read(int p)
{
    uint8_t mask = port_mask[p], res = mask | (port_dr[p] & port_ddr[p]);
    if (((port_ddr[p] & ~mask) != (uint8_t)~mask)) res |= port_ext_read(p) & ~port_ddr[p];
    return res;
}
static void port_write_cb(int p, uint8_t data)
{
    if (p == PB) HM.portb_out = (uint8_t)((HM.portb_out & 0x80) | (data & 0x7f));
}

/* ----------------------------------------------------------------- bus */
static uint8_t  sh_byte(uint32_t o) { return HM.sharedram[o ^ 1]; }          /* big-endian byte view of 16-bit words */
static uint16_t sh_word(uint32_t o) { return (uint16_t)(HM.sharedram[o] | (HM.sharedram[o + 1] << 8)); }
static uint8_t reg_rd8(uint32_t a)
{
    uint32_t r = a & 0xff;
    if (a >= 0xffe0 && a <= 0xffe7) { uint16_t v = addr_[(a - 0xffe0) >> 1]; return (a & 1) ? (uint8_t)(v << 6) : (uint8_t)(v >> 2); }
    switch (a) {
    case 0xffe8: return adcsr;
    case 0xffe9: return adcr;
    case 0xfff2: return syscr;
    case 0xfff4: return iscr;
    case 0xfff5: return ier;
    case 0xfff6: return isr;
    case 0xfff8: return (uint8_t)(icr >> 0);
    case 0xfff9: return (uint8_t)(icr >> 8);
    case 0xff60: return tstr;
    case 0xffb0: return sci[0].smr; case 0xffb1: return sci[0].brr; case 0xffb2: return sci[0].scr; case 0xffb3: return sci[0].tdr;
    case 0xffb4: sci[0].ssr_read = sci[0].ssr; return sci[0].ssr; case 0xffb5: return sci[0].rdr;
    case 0xffb8: return sci[1].smr; case 0xffb9: return sci[1].brr; case 0xffba: return sci[1].scr; case 0xffbb: return sci[1].tdr;
    case 0xffbc: sci[1].ssr_read = sci[1].ssr; return sci[1].ssr; case 0xffbd: return sci[1].rdr;
    case 0xffc5: return port_ddr[P4]; case 0xffc7: return port_read(P4);
    case 0xffc9: return 0xff; case 0xffcb: return port_read(P6);
    case 0xffcd: return 0xff; case 0xffce: return port_read(P7); case 0xffcf: return port_read(P8);
    case 0xffd0: return 0xff; case 0xffd1: return 0xff; case 0xffd2: return port_read(P9); case 0xffd3: return port_read(PA);
    case 0xffd4: return 0xff; case 0xffd6: return port_read(PB);
    }
    if (a >= 0xff64 && a <= 0xff9f) {                       /* ITU channels */
        static const uint32_t chbase[5] = { 0xff64, 0xff6e, 0xff78, 0xff82, 0xff92 };
        for (int ch = 0; ch < 5; ch++) {
            uint32_t b = chbase[ch]; itu_t *t = &itu[ch];
            if (a == b) return t->tcr; if (a == b + 1) return 0; if (a == b + 2) return t->tier; if (a == b + 3) return t->tsr;
            if (a == b + 4) return (uint8_t)(t->tcnt >> 8); if (a == b + 5) return (uint8_t)t->tcnt;
            if (a >= b + 6 && a <= b + 9) { uint16_t v = t->tgr[(a - b - 6) >> 1]; return (a & 1) ? (uint8_t)v : (uint8_t)(v >> 8); }
        }
    }
    logh(a ^ 0x10000, "[h8] on-chip read %06x (generic reg %02x)\n", 0xff0000 | a, regs[r]);
    return regs[r];
}
static void reg_wr8(uint32_t a, uint8_t v)
{
    uint32_t r = a & 0xff;
    if (a == 0xffe8) { uint8_t prev = adcsr; adcsr = (v & 0x7f) | (adcsr & v & 0x80); adc_mode_update(); if (!(prev & 0x20) && (adcsr & 0x20)) adc_start(); return; }
    switch (a) {
    case 0xffe9: adcr = v; adc_mode_update(); return;
    case 0xfff2: syscr = v; return;
    case 0xfff4: iscr = v; update_irq_types(); check_level_irqs(); return;
    case 0xfff5: ier = v; return;
    case 0xfff6: isr &= v; check_level_irqs(); return;
    case 0xfff8: icr = (icr & 0xff00u) | v; return;
    case 0xfff9: icr = (icr & 0x00ffu) | ((uint32_t)v << 8); return;
    case 0xff60: tstr = v; for (int i = 0; i < 5; i++) itu[i].enabled = (v >> i) & 1; return;
    case 0xffb0: sci[0].smr = v; return; case 0xffb1: sci[0].brr = v; return;
    case 0xffb2: { sci_t *s = &sci[0]; s->scr = v; sci_ints(s); return; }
    case 0xffb3: sci[0].tdr = v; return;
    case 0xffb4: { sci_t *s = &sci[0]; if ((s->scr & SCR_TE) && (s->ssr & s->ssr_read & SSR_TDRE) && !(v & SSR_TDRE)) s->ssr &= ~(SSR_TDRE | SSR_TEND);
                   s->ssr = (uint8_t)((s->ssr & (~s->ssr_read | v | SSR_TDRE | SSR_TEND | SSR_MPB) & ~SSR_MPBT) | (v & SSR_MPBT)); s->ssr_read &= s->ssr;
                   if (!s->tx_busy && !(s->ssr & SSR_TDRE)) sci_tx_start(s); sci_ints(s); return; }
    case 0xffb8: sci[1].smr = v; return; case 0xffb9: sci[1].brr = v; return;
    case 0xffba: { sci_t *s = &sci[1]; s->scr = v; sci_ints(s); return; }
    case 0xffbb: sci[1].tdr = v; return;
    case 0xffbc: { sci_t *s = &sci[1]; if ((s->scr & SCR_TE) && (s->ssr & s->ssr_read & SSR_TDRE) && !(v & SSR_TDRE)) s->ssr &= ~(SSR_TDRE | SSR_TEND);
                   s->ssr = (uint8_t)((s->ssr & (~s->ssr_read | v | SSR_TDRE | SSR_TEND | SSR_MPB) & ~SSR_MPBT) | (v & SSR_MPBT)); s->ssr_read &= s->ssr;
                   if (!s->tx_busy && !(s->ssr & SSR_TDRE)) sci_tx_start(s); sci_ints(s); return; }
    case 0xffc5: port_ddr[P4] = v; return; case 0xffc7: port_dr[P4] = v; return;
    case 0xffc9: port_ddr[P6] = v; return; case 0xffcb: port_dr[P6] = v; return;
    case 0xffcd: port_ddr[P8] = v; return; case 0xffcf: port_dr[P8] = v; return;
    case 0xffd0: port_ddr[P9] = v; return; case 0xffd1: port_ddr[PA] = v; return;
    case 0xffd2: port_dr[P9] = v; return;
    case 0xffd3: port_dr[PA] = v; return;
    case 0xffd4: port_ddr[PB] = v; return;
    case 0xffd6: port_dr[PB] = v; port_write_cb(PB, (uint8_t)((v | ~port_ddr[PB]) & ~0)); return;
    }
    if (a >= 0xff64 && a <= 0xff9f) {
        static const uint32_t chbase[5] = { 0xff64, 0xff6e, 0xff78, 0xff82, 0xff92 };
        for (int ch = 0; ch < 5; ch++) {
            uint32_t b = chbase[ch]; itu_t *t = &itu[ch];
            if (a == b) { t->tcr = v; return; } if (a == b + 1) return;
            if (a == b + 2) { t->tier = v; itu_update_ints(t); return; }
            if (a == b + 3) { t->tsr &= (uint8_t)(v | ~7u); itu_update_ints(t); return; }
            if (a == b + 4) { t->tcnt = (uint16_t)((t->tcnt & 0x00ff) | (v << 8)); return; }
            if (a == b + 5) { t->tcnt = (uint16_t)((t->tcnt & 0xff00) | v); return; }
            if (a >= b + 6 && a <= b + 9) { uint16_t *g = &t->tgr[(a - b - 6) >> 1]; *g = (a & 1) ? (uint16_t)((*g & 0xff00) | v) : (uint16_t)((*g & 0x00ff) | (v << 8)); return; }
        }
    }
    logh(a ^ 0x20000, "[h8] on-chip write %06x = %02x (generic reg)\n", 0xff0000 | a, v);
    regs[r] = v;
}

uint8_t h8_rd8(h8_t *c, uint32_t a)
{
    (void)c; a &= 0xffffff;
    if (a < 0x80000) return HM.rom[a];
    if (a < 0x90000) return sh_byte(a - 0x80000);
    if (a >= 0x280000 && a < 0x288000) { c352_catchup(H8.cycles); uint16_t w = c352_read((a - 0x280000) >> 1); return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8); }
    if (a >= 0x300000 && a < 0x300004) return HM.in[a - 0x300000];
    if (a == 0x300010 || a == 0x300011 || a == 0x300030 || a == 0x300031) return 0;
    if (a >= 0xfffd10 && a < 0xffff10) return HM.ram[a - 0xfffd10];
    if (a >= 0xffff20) return reg_rd8(a & 0xffff);
    logh(a ^ 0x30000, "[h8] unmapped read %06x\n", a);
    return 0;
}
void h8_wr8(h8_t *c, uint32_t a, uint8_t v)
{
    (void)c; a &= 0xffffff;
    if (a < 0x80000) return;
    if (a < 0x90000) { HM.sharedram[(a - 0x80000) ^ 1] = v; return; }
    if (a >= 0x280000 && a < 0x288000) { c352_catchup(H8.cycles); c352_write((a - 0x280000) >> 1, (uint16_t)((a & 1) ? v : (v << 8)), (a & 1) ? 0x00ff : 0xff00); return; }
    if (a >= 0x300000 && a < 0x300040) return;
    if (a >= 0xfffd10 && a < 0xffff10) { HM.ram[a - 0xfffd10] = v; return; }
    if (a >= 0xffff20) { reg_wr8(a & 0xffff, v); return; }
    logh(a ^ 0x40000, "[h8] unmapped write %06x = %02x\n", a, v);
}
uint16_t h8_rd16(h8_t *c, uint32_t a)
{
    a &= 0xfffffe;
    if (a >= 0x80000 && a < 0x90000) return sh_word(a - 0x80000);
    if (a >= 0x280000 && a < 0x288000) { c352_catchup(H8.cycles); return c352_read((a - 0x280000) >> 1); }
    return (uint16_t)((h8_rd8(c, a) << 8) | h8_rd8(c, a + 1));
}
void h8_wr16(h8_t *c, uint32_t a, uint16_t v)
{
    a &= 0xfffffe;
    if (a >= 0x80000 && a < 0x90000) { uint32_t o = a - 0x80000; HM.sharedram[o] = (uint8_t)v; HM.sharedram[o + 1] = (uint8_t)(v >> 8); return; }
    if (a >= 0x280000 && a < 0x288000) { c352_catchup(H8.cycles); c352_write((a - 0x280000) >> 1, v, 0xffff); return; }
    h8_wr8(c, a, (uint8_t)(v >> 8)); h8_wr8(c, a + 1, (uint8_t)v);
}
uint32_t h8_rd32(h8_t *c, uint32_t a) { return ((uint32_t)h8_rd16(c, a) << 16) | h8_rd16(c, a + 2); }
void h8_wr32(h8_t *c, uint32_t a, uint32_t v) { h8_wr16(c, a, (uint16_t)(v >> 16)); h8_wr16(c, a + 2, (uint16_t)v); }

uint32_t h8_illegal(h8_t *c, uint32_t at)
{
    fprintf(stderr, "[h8 fatal] illegal/unsupported instruction at %06x (cycle %llu)\n", at, (unsigned long long)c->cycles);
    h8_fatal = 1; return at;
}
uint32_t h8_unknown_pc(h8_t *c, uint32_t at)
{
    fprintf(stderr, "[h8 fatal] jump to unknown code address %06x (cycle %llu) er0-7:", at, (unsigned long long)c->cycles);
    for (int i = 0; i < 8; i++) fprintf(stderr, " %08x", c->er[i]);
    fprintf(stderr, "\n"); h8_fatal = 1; return at;
}

/* ----------------------------------------------------------------- scheduling */
void h8m_vblank(int state)
{
    irq_input_set(1, state);                      /* IRQ1 = screen vblank (namcos12_sub_irq) */
    HM.portb_out = (uint8_t)((state << 7) | (HM.portb_out & 0x7f));
    if (state != adtrg) { adtrg = state; if (!adtrg && adc_trg_ext && !(adcsr & 0x20)) { adcsr |= 0x20; adc_start(); } }
}

void h8m_reset(void)
{
    memset(&H8, 0, sizeof H8); memset(regs, 0xff, sizeof regs);
    memset(pending, 0, sizeof pending); ier = isr = iscr = irq_input = irq_type_edge = 0; icr = 0; syscr = 0x09;
    memset(itu, 0, sizeof itu); const int vb[5] = { 24, 28, 32, 36, 40 };
    for (int i = 0; i < 5; i++) { itu[i].base_vec = vb[i]; itu[i].tgr[0] = itu[i].tgr[1] = 0xffff; itu[i].tier = 0x40; itu[i].enabled = (0xe0 >> i) & 1; }
    tstr = 0xe0;
    memset(sci, 0, sizeof sci); sci[0].base_vec = 52; sci[1].base_vec = 56;
    for (int i = 0; i < 2; i++) { sci[i].brr = 0xff; sci[i].tdr = 0xff; sci[i].ssr = 0x84; sci[i].ssr_read = 0; }
    adcsr = adcr = 0; memset(addr_, 0, sizeof addr_); adc_active = 0; adc_next = 0; adtrg = 1; adc_mode_update();
    memset(port_ddr, 0, sizeof port_ddr); memset(port_dr, 0, sizeof port_dr); port_ddr[P6] = 0x80; port_ddr[P8] = 0xf0;
    H8.ccr = F_I;                                   /* reset: I set */
    H8.er[7] = 0;
    pc = (uint32_t)h8_rd32(&H8, 0) & 0xffffff;      /* reset vector */
    h8_fatal = 0;
}

static void advance(uint64_t delta)
{
    for (int i = 0; i < 5; i++) itu_advance(&itu[i], delta);
    sci_advance(&sci[0]); sci_advance(&sci[1]);
    adc_advance();
}

static int take_interrupt_if_any(void)
{
    int v = pick_interrupt();
    if (!v) return 0;
    H8.halted = 0;
    /* interrupt entry (h8.lst 'irq h', advanced mode): push PC and CCR, set I(+UI), fetch vector */
    H8.er[7] -= 4;
    h8_wr16(&H8, H8.er[7], (uint16_t)((H8.ccr << 8) | ((pc >> 16) & 0xff)));
    h8_wr16(&H8, H8.er[7] + 2, (uint16_t)pc);
    if (syscr & 0x08) H8.ccr |= F_I; else H8.ccr |= F_I | F_UI;
    if (v >= VEC_IRQ0 && v < VEC_IRQ0 + 8) { uint8_t m = 1u << (v - VEC_IRQ0); if (!(irq_type_edge & m) && (irq_input & m)) { /* level: stays */ } else isr &= ~m; }
    pc = h8_rd32(&H8, 4 * v) & 0xffffff;
    H8.cycles += 20;
    return 1;
}

void h8m_run(uint64_t until_states)
{
    while (!h8_fatal && H8.cycles < until_states) {
        if (take_interrupt_if_any()) continue;
        if (H8.halted) {
            uint64_t step = until_states - H8.cycles; if (step > 64) step = 64;
            H8.cycles += step; advance(step); continue;
        }
        h8_blk_fn f = rt_h8_lookup(pc);
        if (!f) { pc = h8_unknown_pc(&H8, pc); break; }
        uint64_t c0 = H8.cycles;
        hring[hring_n++ & 63] = pc;
        pc = f(&H8);
        advance(H8.cycles - c0);
    }
}
int h8m_fatal(void) { return h8_fatal; }
void h8m_dump(void)
{
    fprintf(stderr, "H8: pc=%06x halted=%d ccr=%02x cycles=%llu ier=%02x isr=%02x iscr=%02x icr=%04x pending=%08x%08x syscr=%02x\n", pc, H8.halted, H8.ccr,
            (unsigned long long)H8.cycles, ier, isr, iscr, icr, pending[1], pending[0], syscr);
    fprintf(stderr, "H8 last blocks:");
    for (int i = 0; i < 20; i++) fprintf(stderr, " %06x", hring[(hring_n - 20 + i) & 63]);
    fprintf(stderr, "\nH8 er:"); for (int i = 0; i < 8; i++) fprintf(stderr, " %08x", H8.er[i]);
    fprintf(stderr, "\nITU tstr=%02x:", tstr);
    for (int i = 0; i < 5; i++) fprintf(stderr, " [%d tcr=%02x tier=%02x tsr=%02x tcnt=%04x gra=%04x grb=%04x]", i, itu[i].tcr, itu[i].tier, itu[i].tsr, itu[i].tcnt, itu[i].tgr[0], itu[i].tgr[1]);
    fprintf(stderr, "\nSCI0 scr=%02x ssr=%02x SCI1 scr=%02x ssr=%02x   ADC csr=%02x cr=%02x\n", sci[0].scr, sci[0].ssr, sci[1].scr, sci[1].ssr, adcsr, adcr);
}
uint32_t h8m_pc(void) { return pc; }
uint64_t h8m_states_for_main_cycles(uint64_t main_cycles) { return main_cycles * H8_HZ / CPU_HZ; }
