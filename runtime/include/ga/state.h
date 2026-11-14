/* Save-state serialization for the Gaia Crusaders native runtime.
 *
 * A core state is a tagged, versioned, CRC-protected little-endian blob with
 * every piece of mutable board state: the 68000 (registers, both stack
 * pointers, STOP and interrupt latches, cycle counter), work RAM, sprite RAM,
 * the three 038 tilemap RAMs and their registers, video registers, palette,
 * the latched sprite list and the sprite Z-buffer, the interrupt causes, the
 * YMZ280B (all eight voices) and the scheduler counters.  ROM-derived data is
 * never stored; the state carries a fingerprint of the ROM set instead and is
 * rejected when it does not match. */
#ifndef GA_STATE_H
#define GA_STATE_H

#include <stddef.h>
#include <stdint.h>
#include "ga/board.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GA_STATE_MAGIC "GAIACORE"
#define GA_STATE_VERSION 1u

uint32_t ga_crc32(uint32_t crc, const void *data, size_t size);
uint32_t ga_board_rom_fingerprint(const ga_board *board);
uint8_t *ga_state_save(const ga_board *board, size_t *size);
int ga_state_validate(const ga_board *board, const uint8_t *data, size_t size, char *err, size_t err_size);
int ga_state_load(ga_board *board, const uint8_t *data, size_t size, char *err, size_t err_size);

#ifdef __cplusplus
}
#endif
#endif
