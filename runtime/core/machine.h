/* Golgo 13 native port - machine model (Namco System 12 "coh700b" board, as in MAME namcos12 driver).
 * Hardware behaviour follows MAME (BSD-3-Clause, smf & others): src/devices/cpu/psx/{psx,irq,dma,rcnt}.cpp,
 * src/devices/video/psx.cpp (GPU status), src/mame/namco/namcos12.cpp (board glue). Re-expressed in C. */
#ifndef GOLGO_MACHINE_H
#define GOLGO_MACHINE_H
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include "rt.h"

#define CPU_HZ        50000000ull   /* CXD8661R 100 MHz XTAL / 2 (MAME: "50mhz boards") */
#define PSX_CLK_HZ    33868800ull
#define VBLANK_HZ     60ull
#define RAM_BYTES     0x400000u

typedef struct { uint32_t base, bcr, chcr; } dma_chan_t;
typedef struct { uint32_t count, mode, target; uint64_t start; uint64_t due; int armed; } rcnt_t;

typedef struct machine {
    /* memory */
    uint8_t *ram;                 /* RAM_BYTES */
    uint8_t  scratch[0x400];
    uint8_t  hwram[0x2200];       /* generic r/w registers at 0x1f801000.. that need no model */
    uint8_t *rom_main;            /* 4 MB: BIOS + game program (maincpu:rom) */
    uint8_t *rom_bank;            /* 0x3c00000 */
    uint8_t *rom_sub;             /* 512 KB H8 firmware */
    uint8_t *rom_wave;            /* 16 MB C352 samples */
    uint8_t  sharedram[0x10000];  /* H8 shared RAM (main CPU sees the first 16 KB at 0x1f080000) */
    uint8_t  eeprom[0x800];
    uint8_t *nand;                /* System 10: 6 NAND images (6 x 0x1080000, 528-byte pages) */
    int      sys10;               /* 1 = Namco System 10 board (g13jnr) instead of System 12 */
    /* board */
    uint32_t bankoffset;          /* altbank page index */
    uint32_t dmaoffset;
    uint32_t ram_config, rom_config, exp_base, biu;
    /* irq / dma / counters */
    uint32_t irq_stat, irq_mask;
    dma_chan_t dma[7];
    uint32_t dpcr, dicr;
    rcnt_t   rc[3];
    /* gpu (stub until phase 3) */
    uint32_t gpustat, gpuinfo;
    uint64_t gp0_words, gp0_packets, dma2_lists, dma2_blocks;
    /* time */
    uint64_t next_vblank, vb_end, h8_sync;
    uint64_t nblocks;
    int      in_vblank, prof_noh8, prof_noaudio;       /* profiling switches */
    void   (*audio_sink)(const int16_t *stereo, size_t frames);   /* called once per video frame with the new samples */
    uint64_t frame;
    uint64_t next_event;
    /* run control */
    int      stop, fatal;
    const char *shot_path; uint64_t shot_frame, log_frame;
    uint64_t max_frames;
    int      trace_io;
    /* logging of unknown accesses (each address reported once) */
    uint32_t seen[512]; int nseen;
} machine_t;

extern machine_t G;               /* single machine instance */
extern cpu_t     CPU;

/* loader.c */
typedef struct { const char *id, *title, *version; int year; const char *board; int supported; const char *note; } game_info_t;
int  games_count(void);
const game_info_t *game_info(int i);
int  game_index(const char *id);                                          /* -1 if unknown */
int  rom_select_game(int i);                                              /* choose which ROM table rom_load / rom_import use (default: golgo13) */
void rom_add_search_dir(const char *dir);                                 /* extra folder searched for <id>/ or <id>.zip (besides ./roms and the data folder) */
int  game_find_roms(int i, const char *userroms, char *path, size_t n);   /* 0 none, 1 folder (path), 2 zip (path) */
int  rom_load(const char *dir, char *err, size_t errsz);   /* 0 = ok */
void rom_unload(void);
int  game_is_sys10(void);                             /* the selected game runs on the System 10 board */
int  fs_dir_exists(const char *path);
void fs_abs_path(const char *path, char *out, size_t n);       /* absolute path (for messages) */
void fs_mkdir_p(const char *path);                    /* mkdir -p (used for the data folder and recorded sessions) */
int  rom_import(const char *src, const char *dest, char *err, size_t errsz);   /* src = .zip or folder; copies verified files into dest */

/* machine.c */
void machine_init(void);
void machine_reset(void);
void machine_run(void);
int  machine_run_frames(int n);
void machine_set_free_credit(const uint32_t *addr, int n, int value);   /* keep the game's credit counter(s) (guest addresses) at `value` after every frame; n = 0 disables */
void machine_set_input(int gun_x, int gun_y, int trigger, int sensor, int enter, int up, int down, int start, int coin, int service, int test);           /* runs until G.stop / G.fatal / max_frames */
void machine_dump_ram(const char *path);
void machine_poke8(uint32_t addr, uint8_t v);
uint32_t machine_peek32(uint32_t addr);   /* test tool: read a guest word */
void machine_dump_state(void);
void machine_watch(uint32_t pc);
void machine_save_shot(const char *path);
void log_once(uint32_t key, const char *fmt, ...);

#endif
