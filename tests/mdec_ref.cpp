// Reference wrapper: MAME's mdec.cpp (copied unchanged into build/mdec_ref by the test driver) with C linkage.
#include "emu.h"
#define protected public   // test-only access to device_start/device_reset
#include "mdec.h"
#include "mdec.cpp"
static psxmdec_device *dev;
extern "C" {
void ref_mdec_init(void) { static machine_config mc; dev = new psxmdec_device(mc, "mdec", nullptr, 0); dev->device_start(); dev->device_reset(); }
void ref_mdec_write(uint32_t o, uint32_t d) { dev->write(o, d); }
uint32_t ref_mdec_read(uint32_t o) { return dev->read(o); }
void ref_mdec_dma_write(uint8_t *ram, uint32_t a, int32_t n) { dev->dma_write((uint32_t *)ram, a, n); }
void ref_mdec_dma_read(uint8_t *ram, uint32_t a, int32_t n) { dev->dma_read((uint32_t *)ram, a, n); }
}
