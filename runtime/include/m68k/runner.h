/* Top-level dispatch loop for the statically translated program. */
#ifndef VS_M68K_RUNNER_H
#define VS_M68K_RUNNER_H

#include <stdint.h>
#include "m68k/m68k.h"

#ifdef __cplusplus
extern "C" {
#endif

#define M68K_RUN_OK 0
#define M68K_RUN_MISSING (-1)

/* Run until the CPU's cycle counter reaches target_cycles (each translated
 * instruction charges its MC68000 timing).  Interrupts are delivered between
 * instructions when the SR mask allows them; a STOPped CPU idles to the
 * target.  Returns M68K_RUN_MISSING when execution reaches an address with no
 * translated function. */
int m68k_run_until(m68k_state *s, uint64_t target_cycles);
uint32_t m68k_last_missing_pc(void);
uint64_t m68k_missing_count(void);
/* Fallback interpreter (m68k_interp.c) for code the static analysis missed;
 * strict mode (development) stops at such code instead. */
void m68k_set_strict(int strict);
uint64_t m68k_fallback_instructions(void);
unsigned m68k_fallback_entries(const uint32_t **pcs);

#ifdef __cplusplus
}
#endif
#endif
