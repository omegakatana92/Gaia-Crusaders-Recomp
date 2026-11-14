/* Yamaha YMZ280B PCMD8 (shared component: Cyvern, Gaia Crusaders).
 * Behaviour follows MAME 0.264 src/devices/sound/ymz280b.cpp (BSD-3-Clause,
 * Aaron Giles); no MAME code is linked. */
#ifndef YMZ280B_H
#define YMZ280B_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ymz280b_voice {
    uint8_t playing, ended, keyon, looping, mode;
    uint16_t fnum;
    uint8_t level, pan;
    uint32_t start, stop, loop_start, loop_end, position;
    int32_t signal, step, loop_signal, loop_step;
    uint32_t loop_count;
    int32_t output_left, output_right, output_step, output_pos;
    int16_t last_sample, curr_sample;
    uint8_t irq_schedule;
} ymz280b_voice;

typedef struct ymz280b {
    uint8_t current_register, status_register, irq_state, irq_mask, irq_enable, keyon_enable, ext_mem_enable;
    uint8_t ext_readlatch;
    uint32_t ext_mem_address_hi, ext_mem_address_mid, ext_mem_address;
    ymz280b_voice voice[8];
    /* not part of the chip state: the sample ROM */
    const uint8_t *rom;
    uint32_t rom_size;
} ymz280b;

void ymz280b_reset(ymz280b *z);
uint8_t ymz280b_read(ymz280b *z, unsigned offset);
void ymz280b_write(ymz280b *z, unsigned offset, uint8_t data);
/* Render 'samples' frames at the chip's internal rate (clock / 192) into the
 * accumulators (sum of sample * level per side; MAME's output is that / 512). */
void ymz280b_render(ymz280b *z, int32_t *left, int32_t *right, int samples);
/* Deliver end-of-sample IRQs scheduled by render() ("on the next CPU
 * execution" in MAME); returns the IRQ output line. */
int ymz280b_service(ymz280b *z);
int ymz280b_irq(const ymz280b *z);

#ifdef __cplusplus
}
#endif
#endif
