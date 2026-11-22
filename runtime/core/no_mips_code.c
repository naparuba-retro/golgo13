/* Stand-in for the recompiled main-CPU code, used when generated/ is empty (it is derived from the ROMs and never published, e.g. in CI):
 * the program builds and the menu works, but starting a game shows a message. Run `python tools/setup.py` to generate the code. */
#include <stdio.h>
#include "rt.h"
extern int rt_code_is_stub;
void code_export_all(blk_fn *ram_tbl, uint32_t ram_words, blk_fn *rom_tbl, uint32_t rom_words)
{
    (void)ram_tbl; (void)ram_words; (void)rom_tbl; (void)rom_words; rt_code_is_stub = 1;
    fprintf(stderr, "This build contains no recompiled game code (generated/ is created from your own ROMs: python tools/setup.py).\n");
}
