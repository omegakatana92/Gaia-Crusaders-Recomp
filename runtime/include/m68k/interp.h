/* 68000 fallback interpreter: decodes with a table generated from the
 * translator's own disassembler and timing model (tools/make_m68k_interp.py)
 * and executes through the same semantic helpers as the translated code.
 * Used for code the static analysis did not reach, and for everything in a
 * generic (ROM-independent) build. */
#ifndef M68K_INTERP_H
#define M68K_INTERP_H

#include <stdint.h>
#include "m68k/m68k.h"
#include "m68k/interp_classes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct m68k_operand_form {
    uint8_t kind;       /* M68K_O_* */
    uint8_t reg;        /* 0-7 (0-15 for data/address register numbers where noted) */
    uint32_t value;     /* quick value */
} m68k_operand_form;

typedef struct m68k_decoded {
    uint8_t cls;        /* M68K_I_*; 0 = not a valid 68000 instruction for this runtime */
    uint8_t size;       /* 1, 2, 4 */
    uint8_t cond;
    uint16_t cycles;    /* static part (m68k_timing.cycles) */
    uint8_t extra;      /* shift / bit operation selector */
    m68k_operand_form a, b;
    uint8_t short_branch;
} m68k_decoded;

extern const m68k_decoded m68k_decode_table[65536];

/* Execute one instruction at pc; returns the next pc (0xFFFFFFFF = cannot run). */
uint32_t m68k_interp_step(m68k_state *s, uint32_t pc);

#ifdef __cplusplus
}
#endif
#endif
