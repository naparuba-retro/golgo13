/* Stand-in for the recompiled H8 firmware, used when generated_h8/ is empty (see no_mips_code.c). */
#include "rt.h"
#include "h8_state.h"
h8_blk_fn h8_lookup(uint32_t pc) { (void)pc; return 0; }
