/* Which recompiled code set runs: one set per game (each game's own generated MIPS blocks and H8 firmware blocks).
 * A set is only present if it was generated and compiled in (HAVE_<GAME> defined by the build). */
#include <stdio.h>
#include <string.h>
#include "rt.h"
#include "h8_state.h"

#define NL 10
void code_export_all(blk_fn *ram_tbl, uint32_t ram_words, blk_fn *rom_tbl, uint32_t rom_words);        /* golgo13 */
h8_blk_fn h8_lookup(uint32_t pc);
#ifdef HAVE_G13JNR
void g13jnr_code_export_all(blk_fn *ram_tbl, uint32_t ram_words, blk_fn *rom_tbl, uint32_t rom_words);
#endif
#ifdef HAVE_G13KND
void g13knd_code_export_all(blk_fn *ram_tbl, uint32_t ram_words, blk_fn *rom_tbl, uint32_t rom_words);
h8_blk_fn g13knd_h8_lookup(uint32_t pc);
#endif

typedef struct { const char *id; void (*exp)(blk_fn *, uint32_t, blk_fn *, uint32_t); h8_blk_fn (*h8)(uint32_t); } code_set_t;
static const code_set_t SETS[] = {
    { "golgo13", code_export_all, h8_lookup },
#ifdef HAVE_G13KND
    { "g13knd", g13knd_code_export_all, g13knd_h8_lookup },
#endif
#ifdef HAVE_G13JNR
    { "g13jnr", g13jnr_code_export_all, 0 },   /* System 10: no H8 */
#endif
};
/* 1 if this program contains the recompiled code of that game (a build made without it only has the menu) */
int rt_game_has_code(const char *id)
{
    if (!strcmp(id, "golgo13")) return h8_lookup(0x100) != 0;        /* the stand-in (no_h8_code.c) answers 0 */
    for (size_t i = 0; i < sizeof SETS / sizeof SETS[0]; i++) if (!strcmp(SETS[i].id, id)) return 1;
    return 0;
}
int rt_code_is_stub;                      /* set by runtime/core/no_game_code.c when the build has no recompiled code */
static const code_set_t *cur = &SETS[0];
h8_blk_fn (*rt_h8_lookup)(uint32_t pc) = h8_lookup;

/* 0 = selected, 1 = this build has no code for that game */
int game_code_select(const char *id)
{
    for (size_t i = 0; i < sizeof SETS / sizeof SETS[0]; i++)
        if (!strcmp(SETS[i].id, id)) { cur = &SETS[i]; if (SETS[i].h8) rt_h8_lookup = SETS[i].h8; return 0; }
    return 1;
}
void rt_code_export_all(blk_fn *ram_tbl, uint32_t ram_words, blk_fn *rom_tbl, uint32_t rom_words) { cur->exp(ram_tbl, ram_words, rom_tbl, rom_words); }
