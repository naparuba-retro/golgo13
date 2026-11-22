/* Namco C352 PCM chip (32 voices, 8-bit linear / mu-law). Re-expressed in C from MAME src/devices/sound/c352.cpp
 * (license:BSD-3-Clause; R. Belmont, rewritten by superctr, additional code by cync and the hoot team).
 * Clock 25.4016 MHz / 288 = 88200 Hz; this board uses the front-left/front-right outputs only. */
#ifndef GOLGO_C352_H
#define GOLGO_C352_H
#include <stdint.h>
#include <stddef.h>
#define C352_RATE 88200u
void     c352_init(const uint8_t *rom, size_t rom_size);
void     c352_reset(void);
uint16_t c352_read(uint32_t offset);                       /* offset in 16-bit words */
void     c352_write(uint32_t offset, uint16_t data, uint16_t mask);
void     c352_catchup(uint64_t h8_states);                 /* generate samples up to this H8 time (16.9344 MHz states) */
size_t   c352_take(int16_t *dst, size_t max_frames);       /* pops interleaved stereo frames */
#endif
