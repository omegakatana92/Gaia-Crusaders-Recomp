/* Program table for the generic build (GA_GENERIC_BUILD): no code translated
 * from any ROM is linked, so every instruction runs through the fallback
 * interpreter (m68k_interp.c).  Used for redistributable packages. */
#include "m68k/program_api.h"

void m68k_program_init(void) {}
uint32_t m68k_program_step(m68k_state *s, uint32_t pc) { (void)s; (void)pc; return 0xFFFFFFFFu; }
uint32_t m68k_program_entry_count(void) { return 0; }
int m68k_program_has(uint32_t pc) { (void)pc; return 1; }
