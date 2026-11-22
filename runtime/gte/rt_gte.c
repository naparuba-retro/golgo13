/* Bridge between generated code (rt.h) and the GTE port. */
#include "rt.h"
#include "gte_port.h"
uint32_t rt_gte_mfc2(cpu_t *c, uint32_t n) { (void)c; return gte_getdr((int)n); }
uint32_t rt_gte_cfc2(cpu_t *c, uint32_t n) { (void)c; return gte_getcr((int)n); }
void     rt_gte_mtc2(cpu_t *c, uint32_t n, uint32_t v) { (void)c; gte_setdr((int)n, v); }
void     rt_gte_ctc2(cpu_t *c, uint32_t n, uint32_t v) { (void)c; gte_setcr((int)n, v); }
void     rt_gte_cmd (cpu_t *c, uint32_t cmd25) { (void)c; gte_docop2(cmd25); }
