/* PlayStation SPU (bus side) used by the System 10 board */
#ifndef GOLGO_SPU_H
#define GOLGO_SPU_H
#include <stdint.h>
void     spu_init(void (*irq)(void));
void     spu_mix(int16_t *out, int n);                     /* n stereo samples at 44100 Hz (interleaved L,R) */
uint32_t spu_read(uint32_t p, int sz);            /* p in 0x1f801c00..0x1f801fff */
void     spu_write(uint32_t p, int sz, uint32_t v);
void     spu_dma_write(const uint32_t *words, int32_t nwords);   /* channel 4: RAM -> SPU RAM */
void     spu_dma_read(uint32_t *words, int32_t nwords);          /* channel 4: SPU RAM -> RAM */
#endif
