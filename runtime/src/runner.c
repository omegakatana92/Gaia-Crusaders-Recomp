#include "m68k/runner.h"
#include "m68k/program_api.h"
#include "m68k/interp.h"
#include "board/bus.h"

#include <stdio.h>

static uint32_t last_missing;
static uint64_t missing_count;

static uint32_t history[64];
static unsigned history_at;

static int strict_mode;
static uint64_t fallback_instructions;
static uint32_t fallback_entries[256];
static unsigned fallback_entry_count;

void m68k_set_strict(int strict) { strict_mode = strict; }
uint64_t m68k_fallback_instructions(void) { return fallback_instructions; }
unsigned m68k_fallback_entries(const uint32_t **pcs) { if (pcs) *pcs = fallback_entries; return fallback_entry_count; }

static void note_fallback_entry(uint32_t pc)
{
    for (unsigned i = 0; i < fallback_entry_count; i++) if (fallback_entries[i] == pc) return;
    if (fallback_entry_count < 256) fallback_entries[fallback_entry_count++] = pc;
}

int m68k_run_until(m68k_state *s, uint64_t target_cycles)
{
    int in_fallback = 0;
    while (s->cycles < target_cycles) {
        history[history_at++ & 63u] = s->pc;
        if (s->irq_pending) {
            unsigned mask = (s->sr & 0x0700u) >> 8;
            if (s->irq_level == 7 || mask < s->irq_level) {
                m68k_interrupt(s);
                s->instructions++;
                continue;
            }
            if (s->stopped) { s->cycles = target_cycles; return M68K_RUN_OK; }
        }
        if (s->stopped) { s->cycles = target_cycles; return M68K_RUN_OK; }

        uint32_t next = m68k_program_step(s, s->pc);
        if (next == 0xFFFFFFFFu && !strict_mode && m68k_decode_table[m68k_bus_read16(s->pc & 0xFFFFFF)].cls) {
            /* code the static analysis did not reach: run it on the fallback
             * interpreter (same helpers, same timing) until translated code */
            if (!in_fallback) note_fallback_entry(s->pc & 0xFFFFFF);
            in_fallback = 1;
            next = m68k_interp_step(s, s->pc);
            fallback_instructions++;
        } else if (next != 0xFFFFFFFFu) {
            in_fallback = 0;
        }
        if (next == 0xFFFFFFFFu) {
            last_missing = s->pc;
            missing_count++;
            if (missing_count <= 32) {
                fprintf(stderr, "missing translated code at %06X (after %llu instructions)\n",
                        s->pc, (unsigned long long)s->instructions);
                fprintf(stderr, "last PCs:");
                for (unsigned i = 0; i < 24; i++)
                    fprintf(stderr, " %06X", history[(history_at - 1u - i) & 63u]);
                fprintf(stderr, "\nregisters: D0=%08X D1=%08X D2=%08X D3=%08X "
                        "A0=%08X A1=%08X A2=%08X A3=%08X A4=%08X A5=%08X A6=%08X A7=%08X\n",
                        s->d[0], s->d[1], s->d[2], s->d[3],
                        s->a[0], s->a[1], s->a[2], s->a[3],
                        s->a[4], s->a[5], s->a[6], s->a[7]);
            }
            return M68K_RUN_MISSING;
        }
        s->pc = next;
        s->instructions++;
    }
    return M68K_RUN_OK;
}

uint32_t m68k_last_missing_pc(void)
{
    return last_missing;
}

uint64_t m68k_missing_count(void)
{
    return missing_count;
}
