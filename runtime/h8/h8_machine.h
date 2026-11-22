/* H8/3002 sub-CPU machine interface (see h8_machine.c) */
#ifndef GOLGO_H8_MACHINE_H
#define GOLGO_H8_MACHINE_H
#include <stdint.h>
#include "h8_state.h"

typedef struct h8m {
    const uint8_t *rom;       /* 512 KB firmware, big-endian view */
    uint8_t *sharedram;       /* 64 KB, 16-bit words stored little-endian (shared with the main CPU's first 16 KB window) */
    uint8_t  ram[0x200];      /* on-chip RAM 0xfffd10-0xffff0f */
    uint8_t  in[4];           /* 0x300000-3 : JAMMA-style inputs (active low) */
    uint16_t *c352_regs;      /* C352 register file (0x280000-0x287fff) */
    uint16_t adc_in[4];       /* 10-bit analog inputs (gun X/Y ...) */
    uint8_t  dsw;
    uint8_t  portb_out;
} h8m_t;
extern h8_t  H8;
extern h8m_t HM;
void     h8m_reset(void);
void     h8m_run(uint64_t until_states);
void     h8m_vblank(int state);
int      h8m_fatal(void);
void     h8m_dump(void);
uint32_t h8m_pc(void);
uint64_t h8m_states_for_main_cycles(uint64_t main_cycles);
#endif
