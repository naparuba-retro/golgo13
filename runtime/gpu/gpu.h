/* Namco System 12 GPU (CXD8654Q-like "type 2": 2 MB VRAM = 1024x1024x16) - software renderer.
 * Packet formats and the System-12 specific draw-mode layout follow MAME src/devices/video/psx.cpp
 * (license:BSD-3-Clause, copyright-holders:smf); the rasteriser itself is an independent implementation. */
#ifndef GOLGO_GPU_H
#define GOLGO_GPU_H
#include <stdint.h>
#include <stddef.h>

#define VRAM_W 1024
#define VRAM_H 1024

void     gpu_reset(void);
void     gpu_gp0_write(uint32_t word);               /* GP0 port / DMA stream */
void     gpu_gp1_write(uint32_t word);               /* GP1 control port */
uint32_t gpu_status(uint64_t frame, uint32_t vpos);  /* GPUSTAT (0x1f801814) */
uint32_t gpu_read(void);                             /* GPUREAD (0x1f801810) / DMA read word */
void     gpu_vblank(void);                           /* toggles interlace/field bits */
/* Display: renders the current display area into RGBA8 (4 bytes/pixel, R,G,B,255). Returns size.
 * buf must hold at least 1024*512*4 bytes. */
void     gpu_get_display(uint8_t *buf, int *w, int *h);
uint16_t *gpu_vram(void);
void gpu_overlay_clear(void);
int gpu_overlay_load(const char *path, char *msg, size_t msgsz);   /* translation overlays (G13O), see runtime/gpu/gpu.c */
extern const char *gpu_quadcap_path; extern uint64_t gpu_quadcap_frame;   /* scene capture for the translation tools */
extern const char *gpu_texdump_dir; extern uint64_t gpu_texdump_from, gpu_texdump_to;
extern int gpu_trace_loads; extern uint64_t gpu_frame_no; extern uint32_t gpu_src_addr;
extern int gpu_nodraw, gpu_log, gpu_trace_x, gpu_trace_y;                                  /* debug: log every primitive to stderr */
uint64_t gpu_stat_words(void);
uint64_t gpu_stat_prims(void);
#endif
