/* Namco System 10 board glue (see sys10.c) */
#ifndef GOLGO_SYS10_H
#define GOLGO_SYS10_H
#include <stdint.h>
void sys10_build_bios(void);          /* fills G.rom_main (4 MB) from the NAND images */
void sys10_reset(void);
void sys10_tick(void);                /* 100 Hz I/O interrupt source */
int  sys10_read(uint32_t p, int sz, uint32_t *out);     /* p = physical address 0x1fxxxxxx; 1 = handled */
int  sys10_write(uint32_t p, int sz, uint32_t v);
void sys10_dma5(uint32_t *ram32, uint32_t addr, int32_t nwords);   /* PIO DMA channel 5: NAND -> RAM */
void sys10_set_gun(int gun_x, int gun_y);   /* 0..511 light-gun position (fed to the MCU ADC mailbox) */
void sys10_set_inputs(int trigger, int sensor, int enter, int up, int down, int start, int coin);
#endif
