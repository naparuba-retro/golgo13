/* PlayStation MDEC (see mdec.c). RAM is passed as bytes (little-endian 32-bit words), addresses are physical RAM offsets. */
#ifndef GOLGO_MDEC_H
#define GOLGO_MDEC_H
#include <stdint.h>
extern uint64_t mdec_blocks;
void     mdec_reset(void);
void     mdec_dma_write(uint8_t *ram, uint32_t address, int32_t size_words);   /* DMA channel 0 (to MDEC) */
void     mdec_dma_read(uint8_t *ram, uint32_t address, int32_t size_words);    /* DMA channel 1 (from MDEC) */
void     mdec_write(uint32_t offset, uint32_t data);                            /* 0x1f801820 / 0x1f801824 */
uint32_t mdec_read(uint32_t offset);
#endif
