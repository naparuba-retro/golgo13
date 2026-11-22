/* H8/3002 (sub CPU) state and bus interface used by generated code (generated_h8/) */
#ifndef GOLGO_H8_STATE_H
#define GOLGO_H8_STATE_H
#include <stdint.h>
typedef struct h8 {
    uint32_t er[8];          /* ER0..ER7 (ER7 = SP) */
    uint8_t  ccr;            /* I UI H U N Z V C */
    uint8_t  halted;         /* SLEEP */
    uint64_t cycles;         /* H8 states (approximate: used to pace peripherals) */
    void    *user;
} h8_t;
typedef uint32_t (*h8_blk_fn)(h8_t *c);

/* bus (runtime): 24-bit addresses, big-endian 16-bit words; 32-bit = two 16-bit accesses */
uint8_t  h8_rd8 (h8_t *c, uint32_t a);
uint16_t h8_rd16(h8_t *c, uint32_t a);
uint32_t h8_rd32(h8_t *c, uint32_t a);
void     h8_wr8 (h8_t *c, uint32_t a, uint8_t v);
void     h8_wr16(h8_t *c, uint32_t a, uint16_t v);
void     h8_wr32(h8_t *c, uint32_t a, uint32_t v);

uint32_t h8_illegal(h8_t *c, uint32_t pc);      /* illegal/unsupported instruction: reports and stops */
uint32_t h8_unknown_pc(h8_t *c, uint32_t pc);   /* jump to code that was not recompiled: reports and stops */
h8_blk_fn h8_lookup(uint32_t pc);               /* generated table (golgo13) */
extern h8_blk_fn (*rt_h8_lookup)(uint32_t pc);  /* the selected game's table */
#endif
