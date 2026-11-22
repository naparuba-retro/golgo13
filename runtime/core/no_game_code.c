/* Stand-in for the recompiled game code, used when the build has none (generated/ is ROM-derived and never published, e.g. in CI):
 * the program builds and the menu works, but starting a game stops with a message. Generate the code locally to play (see README). */
#include <stdio.h>
#include "rt.h"
#include "h8_state.h"
void code_export_all(blk_fn *ram_tbl, uint32_t ram_words, blk_fn *rom_tbl, uint32_t rom_words)
{
    (void)ram_tbl; (void)ram_words; (void)rom_tbl; (void)rom_words;
    fprintf(stderr, "This build contains no recompiled game code (generated/ is created from your own ROMs, see README: \"Construire\").\n");
}
h8_blk_fn h8_lookup(uint32_t pc) { (void)pc; return 0; }
