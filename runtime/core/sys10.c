/* Namco System 10 "MEM(N)" board glue (Golgo 13 Juusei no Requiem): NAND controller + type-2 stream decrypter, I/O registers, block table RAM.
 * Behaviour follows MAME src/mame/namco/namcos10.cpp and ns10crypt.cpp (BSD-3-Clause), re-expressed in C. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"
#include "sys10.h"
#include "mp3.h"

#define NAND_CHIPS 6
#define NAND_PAGE  0x210u
#define NAND_CHIP_BYTES 0x1080000u

/* ---- scrambling of the non-encrypted NAND data (g13jnr), XOR 0xaaaa then a fixed bit permutation ---- */
static uint16_t uns_tab[65536]; static int uns_ready;
static void uns_init(void)
{
    static const int order[16] = { 0xe, 0xd, 0xc, 0xf, 0x9, 0xb, 0x8, 0xa, 0x6, 0x7, 0x4, 0x5, 0x1, 0x3, 0x0, 0x2 };
    for (uint32_t v = 0; v < 65536; v++) {
        uint32_t w = v ^ 0xaaaau, r = 0;
        for (int i = 0; i < 16; i++) r = (r << 1) | ((w >> order[i]) & 1);
        uns_tab[v] = (uint16_t)r;
    }
    uns_ready = 1;
}

/* ---- type-2 decrypter ---- */
static const uint64_t E_MASK[16] = {
    0x00005600001c0582ull, 0x0000000000004024ull, 0x0000010212403000ull, 0x0000000000008404ull, 0x0000005121060021ull, 0x0000010212402001ull, 0x0000000001024000ull, 0x0000000000000840ull,
    0x00000981c2100148ull, 0x0000000020108400ull, 0x0000000008110134ull, 0x0000000000000003ull, 0x00004c9801080102ull, 0x0000000040860083ull, 0x0000000000000001ull, 0x0000000000000288ull };
static const uint64_t D_MASK[16] = {
    0x0000441800340584ull, 0x0000000000004028ull, 0x0000010212800000ull, 0x0000000000000404ull, 0x0000485022000041ull, 0x0000010212804001ull, 0x0000000001028000ull, 0x0000000000000841ull,
    0x00000e0104080150ull, 0x0000000023208400ull, 0x0000000040110104ull, 0x0000000000000003ull, 0x0000470001100102ull, 0x00000000408c0083ull, 0x0000000000000002ull, 0x0000000000000308ull };
static const int INIT_SBOX[16] = { 0, 12, 13, 6, 2, 4, 9, 8, 11, 1, 7, 15, 10, 5, 14, 3 };
static int parity64(uint64_t v) { v ^= v >> 32; v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return (int)(v & 1); }
static struct { int active; uint16_t mask; uint64_t pc, pp; } dc;
static void dc_activate(int iv)
{
    int s = INIT_SBOX[iv & 15];
    dc.pc = (uint64_t)(((s >> 3) & 1) << 15 | ((s >> 2) & 1) << 12 | ((s >> 1) & 1) << 11 | (s & 1) << 8); dc.pp = 0; dc.mask = 0; dc.active = 1;
}
static uint16_t dc_decrypt(uint16_t c)
{
    uint16_t p = (uint16_t)(c ^ dc.mask);
    dc.pc = (dc.pc << 16) ^ c; dc.pp = (dc.pp << 16) ^ p;
    uint16_t m = 0;
    for (int j = 15; j >= 0; j--) { m = (uint16_t)(m << 1); m ^= (uint16_t)parity64(E_MASK[j] & dc.pc); m ^= (uint16_t)parity64(D_MASK[j] & dc.pp); }
    m ^= 0x9546;
    { uint64_t masks = dc.pc ^ dc.pp; m ^= (uint16_t)((1u & (uint32_t)(masks >> 6) & (uint32_t)(masks >> 10)) << 14); }
    dc.mask = m; return p;
}

/* ---- NAND controller (registers at 0x1f4xxxxx) ---- */
static struct { uint32_t dev, addr, ctrl, pos; int cmd; int id_phase; } nd;     /* pos: byte offset in the chip image of the next byte to read */
static uint8_t *nand_base(void) { return G.nand; }
static uint8_t nand_byte(void)
{
    if (nd.cmd == 0x90) { static const uint8_t id[2] = { 0xec, 0x73 }; return id[(nd.id_phase++) & 1]; }
    if (nd.cmd == 0x70) return 0xc0;
    if (nd.dev >= NAND_CHIPS || !G.nand) return 0xff;
    if (nd.pos >= NAND_CHIP_BYTES) return 0xff;
    return G.nand[(size_t)nd.dev * NAND_CHIP_BYTES + nd.pos++];
}
static uint16_t nand_data_word(void)
{
    uint16_t w = (uint16_t)((nand_byte() << 8) | nand_byte());
    if (dc.active) return dc_decrypt(w);
    if (nd.dev == 0 && (((nd.addr >> 8) >> 5) < 2)) return w;           /* blocks 0 and 1 of chip 0 are plain */
    return uns_tab[w];
}
static void nand_addr_update(void)
{
    uint32_t page = (nd.addr >> 8) & 0xffff, col = nd.addr & 0xff;
    uint32_t base = page * NAND_PAGE;
    if (nd.cmd == 0x01) col += 0x100; else if (nd.cmd == 0x50) col += 0x200;
    nd.pos = base + col;
}

/* ---- I/O ---- */
/* MEMP3 board (MP3 decoder + TLCS-900 MCU): no MCU yet, only the host-side registers and the shared RAM window are modelled */
static uint16_t *mcu_ram; static uint32_t mcu_bank; static int mcu_started, win_log;
static uint8_t blocktab[0x100000];                 /* 0x1f500000: block redirection table, filled by the BIOS */
static uint32_t int_reg; static int exio_bit = 7, exio_byte;
static uint32_t in1_val = 0xf7ffffffu;             /* idle: active-low bits 1, scope sensor (bit 27, active high) 0 */
static uint32_t sys_val = 0xffffu;
/* I2C bit-banged EEPROM line (MAME namcos10_state::i2c_update): the device acknowledges every byte */
static uint32_t i2c_host_clock = 1, i2c_host_data = 1, i2c_dev_data = 1, i2c_prev_clock = 1, i2c_prev_data = 1; static int i2c_mode, i2c_bit; static uint32_t i2c_byte;
static void i2c_update(void)
{
    uint32_t clock = i2c_host_clock & 1, data = i2c_dev_data & i2c_host_data & 1;
    if (i2c_prev_data == data && i2c_prev_clock == clock) return;
    switch (i2c_mode) {
    case 0: if (clock && !data) { i2c_byte = 0; i2c_bit = 7; i2c_mode = 1; } break;                     /* idle: start bit */
    case 1:
        if (clock && data && !i2c_prev_data) i2c_mode = 0;                                              /* stop bit */
        else if (clock && !i2c_prev_clock) { i2c_byte |= data << i2c_bit; i2c_bit--; if (i2c_bit < 0) { i2c_mode = 2; i2c_dev_data = 0; data = 0; } }
        break;
    case 2: if (clock && !i2c_prev_clock) i2c_mode = 3; break;                                         /* ack on */
    case 3: if (!clock && i2c_prev_clock) { i2c_dev_data = 1; data = i2c_host_data & 1; i2c_byte = 0; i2c_bit = 7; i2c_mode = 1; } break;
    }
    i2c_prev_data = data; i2c_prev_clock = clock;
}

/* The MCU's ADC handler stores AN0..AN3 (10 bit) at MCU 0xfffff0..f6 as (v << 6) | (v >> 4); AN0 = gun Y (reversed), AN1 = gun X. */
static uint16_t adc_word(int v10) { v10 &= 0x3ff; return (uint16_t)((v10 << 6) | (v10 >> 4)); }
static uint16_t gun_ch[4] = { 0x7fff, 0x7fff, 0, 0 };
void sys10_set_gun(int gun_x, int gun_y)
{
    int x10 = gun_x * 1023 / 511, y10 = 1023 - gun_y * 1023 / 511;
    gun_ch[0] = adc_word(y10); gun_ch[1] = adc_word(x10);
}
void sys10_set_inputs(int trigger, int sensor, int enter, int up, int down, int start, int coin)
{
    uint32_t v = 0xf7ffffffu;
    if (trigger) v &= ~0x20u; if (enter) v &= ~0x10u; if (up) v &= ~0x08u; if (down) v &= ~0x04u; if (start) v &= ~0x80u; if (coin) v &= ~0x20000000u;
    if (sensor) v |= 0x08000000u;
    in1_val = v;
}

void sys10_build_bios(void)
{
    if (!uns_ready) uns_init();
    if (!G.rom_main) G.rom_main = calloc(1, 0x400000);
    memset(G.rom_main, 0x55, 0x400000);
    static const struct { uint32_t page, count, dst; } cp[2] = { { 0x40, 0xe0, 0 }, { 0x120, 0x1f00, 0x20000 } };
    for (int k = 0; k < 2; k++)
        for (uint32_t p = cp[k].page; p < cp[k].page + cp[k].count; p++) {
            const uint8_t *s = G.nand + (size_t)p * NAND_PAGE; uint16_t *d = (uint16_t *)(G.rom_main + cp[k].dst + (p - cp[k].page) * 0x200u);
            for (int i = 0; i < 0x200; i += 2) *d++ = uns_tab[(uint16_t)((s[i] << 8) | s[i + 1])];
        }
}

void sys10_reset(void)
{
    if (getenv("S10XHAIR")) sys_val &= ~1u;   /* dip "Show Crosshair" on: the game draws its own aim point (debug) */
    memset(&nd, 0, sizeof nd); memset(&dc, 0, sizeof dc); memset(blocktab, 0, sizeof blocktab);
    int_reg = 0; exio_bit = 7; exio_byte = 0; i2c_host_clock = i2c_host_data = i2c_dev_data = i2c_prev_clock = i2c_prev_data = 1; i2c_mode = 0; i2c_bit = 0; i2c_byte = 0;
}

void sys10_tick(void) { int_reg |= 8; }

static int sys10_read_(uint32_t p, int sz, uint32_t *out); static int sys10_write_(uint32_t p, int sz, uint32_t v); static void sys10_mcu_command(void);
static int dbg = -1; static int dbg_n;
static void dbgf(const char *what, uint32_t p, int sz, uint32_t v) { if (dbg < 0) dbg = getenv("S10DBG") != NULL; if (dbg && dbg_n < 4000) { dbg_n++; fprintf(stderr, "s10 %s %08x/%d = %08x\n", what, p, sz, v); } }
int sys10_read(uint32_t p, int sz, uint32_t *out)
{
    uint32_t o = p - 0x10000000u; *out = 0;
    int rc = sys10_read_(p, sz, out); if (rc && !(o >= 0x0f450000u && o < 0x0f450004u)) dbgf("R", p, sz, *out); return rc;
}
static int sys10_read_(uint32_t p, int sz, uint32_t *out)
{
    uint32_t o = p - 0x10000000u; *out = 0;
    if (p >= 0x1f500000u && p < 0x1f600000u) { uint32_t v = 0; memcpy(&v, &blocktab[p - 0x1f500000u], (size_t)sz); *out = v; return 1; }
    if (p >= 0x1f320000u && p < 0x1f340000u && mcu_started && getenv("S10WIN") && win_log < 300000) { win_log++; uint32_t i0 = ((mcu_bank & 63) * 0x10000u + ((p - 0x1f320000u) >> 1)); fprintf(stderr, "win R bank=%u mcu=%06x/%d = %04x\n", mcu_bank, 0x800000u + i0 * 2, sz, mcu_ram ? mcu_ram[i0] : 0); }
    if ((mcu_bank & 63) == 63 && p >= 0x1f33fff0u && p < 0x1f33fff8u) { *out = gun_ch[(p - 0x1f33fff0u) >> 1]; return 1; }   /* ADC mailbox written by the MCU firmware */
    if (p >= 0x1f320000u && p < 0x1f340000u) { if (!mcu_ram) mcu_ram = (uint16_t *)calloc(0x400000, 2); uint32_t i = ((mcu_bank & 63) * 0x10000u + ((p - 0x1f320000u) >> 1)); *out = sz == 4 ? (mcu_ram[i] | ((uint32_t)mcu_ram[i + 1] << 16)) : mcu_ram[i]; return 1; }
    if (p == 0x1f300006u) { *out = 1; return 1; }                         /* subcomm_busy_r: never busy (no MCU) */
    if (p == 0x1f30000eu) { *out = 1; return 1; }                         /* unk_status1_r: firmware upload allowed */
    switch (o & ~3u) {
    case 0x0fba0000u: if ((o & 2) == 0) { *out = (sz == 4) ? ((sys_val) | 0) : sys_val; return 1; }   /* io_system_r (16 bit) / exio ident */
                     { /* 0xba0002: exio_ident_r */
                       uint32_t res = exio_byte >= 0x20 ? 3u : (((0xffu >> exio_bit) & 1) ? 3u : 0u);
                       exio_bit--; if (exio_bit == -1) { exio_bit = 7; exio_byte++; } *out = res; return 1; }
    case 0x0fba0004u: { uint32_t v = in1_val; if (sz == 2) v = (o & 2) ? (v >> 16) : (v & 0xffff); else if (sz == 1) v = (v >> ((o & 3) * 8)) & 0xff; *out = v; return 1; }
    case 0x0fba0008u: if (o == 0x0fba0008u) *out = i2c_host_clock & 1; else *out = i2c_dev_data & i2c_host_data & 1; return 1;
    case 0x0fba0010u: case 0x0fba0014u: break;
    case 0x0fba0018u: if (o == 0x0fba001au) { *out = int_reg; return 1; } break;
    case 0x0fbf0000u: *out = 1; return 1;                                  /* cdrom_unk_r: drive present */
    case 0x0f400000u: *out = 0; return 1;                                  /* nand_rnb_r: ready */
        case 0x0f470000u: *out = nd.ctrl; return 1;
    default: break;
    }
    if (o >= 0x0f450000u && o < 0x0f450004u) { uint16_t w = nand_data_word(); *out = w; return 1; }
    if (o >= 0x0fbc0000u && o < 0x0fbc0010u) { *out = 0; return 1; }       /* ATA cs0: no drive */
    if (o >= 0x0fe40000u && o < 0x0fe48004u) return 1;
    return 0;
}

int sys10_write(uint32_t p, int sz, uint32_t v)
{
    dbgf("W", p, sz, v); return sys10_write_(p, sz, v);
}
static int sys10_write_(uint32_t p, int sz, uint32_t v)
{
    uint32_t o = p - 0x10000000u; (void)sz;
    if (p >= 0x1f500000u && p < 0x1f600000u) { memcpy(&blocktab[p - 0x1f500000u], &v, (size_t)sz); return 1; }
    if (p >= 0x1f320000u && p < 0x1f340000u && mcu_started && getenv("S10WIN") && win_log < 300000) { win_log++; uint32_t i0 = ((mcu_bank & 63) * 0x10000u + ((p - 0x1f320000u) >> 1)); fprintf(stderr, "win W bank=%u mcu=%06x/%d = %04x\n", mcu_bank, 0x800000u + i0 * 2, sz, v & 0xffff); }
    if (p >= 0x1f320000u && p < 0x1f340000u) { if (!mcu_ram) mcu_ram = (uint16_t *)calloc(0x400000, 2); uint32_t i = ((mcu_bank & 63) * 0x10000u + ((p - 0x1f320000u) >> 1)); mcu_ram[i] = (uint16_t)v; if (sz == 4) mcu_ram[i + 1] = (uint16_t)(v >> 16); return 1; }
    if (p == 0x1f300000u && (v & 0xffff) == 1) mcu_started = 1;
    if (p == 0x1f300000u && (v & 0xffff) == 1 && getenv("S10MCU") && mcu_ram) { FILE *f = fopen(getenv("S10MCU"), "wb"); if (f) { fwrite(mcu_ram, 2, 0x400000, f); fclose(f); } }
    if (p == 0x1f300006u) { sys10_mcu_command(); return 1; }
    if (p == 0x1f300000u) return 1;                  /* firmware_write_w / subcomm_int_w: nothing to start yet */
    if (p == 0x1f30000cu) { mcu_bank = v & 0xffff; return 1; }
    if (o >= 0x0f380000u && o < 0x0f380004u) { if (getenv("S10NAND")) fprintf(stderr, "crypto %04x\n", v & 0xffff); if (v & 0x8000u) dc_activate((int)(v & 0xf)); else dc.active = 0; return 1; }   /* crypto switch */
    if (o >= 0x0f410000u && o < 0x0f410004u) { nd.cmd = (int)(v & 0xff); nd.id_phase = 0; if (nd.cmd == 0x00 || nd.cmd == 0x01 || nd.cmd == 0x50) nand_addr_update(); return 1; }
    if (o >= 0x0f420000u && o < 0x0f420004u) { nd.addr = (nd.addr & 0xffff00u) | (v & 0xff); nand_addr_update(); return 1; }
    if (o >= 0x0f430000u && o < 0x0f430004u) { nd.addr = (nd.addr & 0xff00ffu) | ((v & 0xff) << 8); nand_addr_update(); return 1; }
    if (o >= 0x0f440000u && o < 0x0f440004u) { nd.addr = (nd.addr & 0x00ffffu) | ((v & 0xff) << 16); nand_addr_update(); return 1; }
    if (o >= 0x0f450000u && o < 0x0f450004u) return 1;                                   /* NAND writes are ignored */
    if (o >= 0x0f460000u && o < 0x0f460004u) { if ((nd.ctrl & 4) == 0) nd.dev = v & 0xffff; return 1; }
    if (o >= 0x0f470000u && o < 0x0f470004u) { nd.ctrl = v & 0xffff; return 1; }
    if (o >= 0x0fba0002u && o < 0x0fba0004u) { exio_bit = 7; exio_byte = 0; return 1; }  /* exio_ident_w */
    if (o == 0x0fba0008u) { i2c_host_clock = v & 1; i2c_update(); return 1; }
    if (o == 0x0fba000au) { i2c_host_data = v & 1; i2c_update(); return 1; }
    if (o >= 0x0fba0012u && o < 0x0fba001au) { int_reg &= ~(1u << ((o - 0x0fba0012u) >> 1)); return 1; }
    if ((o >= 0x0fb20000u && o < 0x0fb20004u) || (o >= 0x0fb60000u && o < 0x0fb60004u)) return 1;
    if (o >= 0x0fbc0000u && o < 0x0fbc0010u) return 1;
    if (o >= 0x0fe40000u && o < 0x0fe48004u) return 1;
    return 0;
}

/* MCU command mailbox (bank 63, word 0 = command letter, words 1.. = arguments). Track table at word 8: { u32 offset, u32 size } per track, data at that offset of the board RAM. */
static void sys10_mcu_command(void)
{
    if (!mcu_ram) return;
    const uint16_t *w = &mcu_ram[63u * 0x10000u]; int cmd = w[0] & 0xff, a1 = w[1], a2 = w[2];
    if (getenv("S10CMD")) fprintf(stderr, "mcu cmd %c(%d) %04x %04x\n", cmd >= 32 && cmd < 127 ? cmd : 63, cmd, a1, a2);
    static int cur_track = -1, loop = 1;
    switch (cmd) {
    case 'L': break;                                                        /* (arg 0x10 in every call seen) */
    case 'A': if (a1) cur_track = a1; else if (cur_track < 0) cur_track = 0; break;   /* (assumed) A n = select track n; A 0 = keep */
    case 'P': {
        int t = cur_track < 0 ? 0 : cur_track; if (t >= 12) break;
        { static int dumped; if (!dumped && getenv("S10CMD")) { dumped = 1; for (int e = 0; e < 24; e++) fprintf(stderr, "  tab[%d] off=%05x size=%05x\n", e, w[8 + e * 4] | ((uint32_t)w[9 + e * 4] << 16), w[10 + e * 4] | ((uint32_t)w[11 + e * 4] << 16)); } }
        uint32_t off = w[8 + t * 4] | ((uint32_t)w[9 + t * 4] << 16), sz = w[10 + t * 4] | ((uint32_t)w[11 + t * 4] << 16);
        if (getenv("S10CMD")) fprintf(stderr, "  play track %d off=%x size=%x loop=%d\n", t, off, sz, loop);
        loop = sz >= 0x70000u;                                                 /* (assumed) long tracks are BGM and repeat */
        if (sz && off + sz <= 0x800000u) mp3_play((const uint8_t *)mcu_ram + off, sz, loop); cur_track = -1; break; }
    case 'S': mp3_stop(); break;
    case 'V': mp3_set_volume(a1 >= 8 ? 256 : a1 * 32, a1 >= 8 ? 256 : a1 * 32); break;
    default: break;
    }
}

void sys10_dma5(uint32_t *ram32, uint32_t addr, int32_t nwords)
{
    if (getenv("S10NAND")) fprintf(stderr, "nanddma dev=%u page=%u col=%u nwords=%d ram=%08x dc=%d\n", nd.dev, (nd.addr >> 8) & 0xffff, nd.addr & 0xff, nwords, addr, dc.active);
    uint16_t *ptr = (uint16_t *)&ram32[addr / 4];
    uint32_t room = (RAM_BYTES - (addr & (RAM_BYTES - 1))) / 2;
    int32_t n = nwords * 2; if ((uint32_t)n > room) n = (int32_t)room;
    for (int32_t i = 0; i < n; i++) ptr[i] = nand_data_word();
}
