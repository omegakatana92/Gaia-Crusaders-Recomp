/* Interface of the generated translation unit (build/generated/program_*.c).
 * Declared here so the runtime compiles without a ROM; the generated
 * program.h includes this header. */
#ifndef M68K_PROGRAM_API_H
#define M68K_PROGRAM_API_H
#include <stdint.h>
#include "m68k/m68k.h"
void m68k_program_init(void);
uint32_t m68k_program_step(m68k_state *s, uint32_t pc);
uint32_t m68k_program_entry_count(void);
int m68k_program_has(uint32_t pc);
#endif
