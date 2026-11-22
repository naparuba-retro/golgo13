/* GTE (COP2) C port - see gte.c (derived from MAME gte.cpp, BSD-3-Clause, (c) smf) */
#ifndef GOLGO_GTE_PORT_H
#define GOLGO_GTE_PORT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void     gte_reset(void);
int      gte_docop2(uint32_t gteop);          /* returns 0 for unknown command */
uint32_t gte_getdr(int reg);
void     gte_setdr(int reg, uint32_t value);
uint32_t gte_getcr(int reg);
void     gte_setcr(int reg, uint32_t value);
const uint32_t *gte_dr_raw(void);              /* snapshot of data registers (for tests) */
const uint32_t *gte_cr_raw(void);              /* snapshot of control registers */
#ifdef __cplusplus
}
#endif
#endif
