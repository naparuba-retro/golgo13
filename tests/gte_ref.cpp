// Reference wrapper: MAME's gte.cpp compiled unchanged (shim for emu.h), exposed with C linkage.
#include "emu.h"
#include "../third_party/mame/src/devices/cpu/psx/gte.h"
#include "../third_party/mame/src/devices/cpu/psx/gte.cpp"
static gte g_ref;
extern "C" {
void ref_reset(void) { g_ref = gte(); for (int i = 0; i < 32; i++) { g_ref.m_cp2cr[i].d = 0; g_ref.m_cp2dr[i].d = 0; } }
void ref_setdr(int r, uint32_t v) { g_ref.setcp2dr(0, r, v); }
void ref_setcr(int r, uint32_t v) { g_ref.setcp2cr(0, r, v); }
uint32_t ref_getdr(int r) { return g_ref.getcp2dr(0, r); }
uint32_t ref_getcr(int r) { return g_ref.getcp2cr(0, r); }
int ref_cmd(uint32_t op) { return g_ref.docop2(0, (int)op); }
void ref_dump(uint32_t *dr, uint32_t *cr) { for (int i = 0; i < 32; i++) { dr[i] = g_ref.m_cp2dr[i].d; cr[i] = g_ref.m_cp2cr[i].d; } }
}
