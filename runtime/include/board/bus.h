/* 68000 bus interface used by the translated program and the 68000
 * semantic helpers.  The board (runtime/src/board.c) implements it. */
#ifndef BOARD_BUS_H
#define BOARD_BUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t  m68k_bus_read8(uint32_t address);
uint16_t m68k_bus_read16(uint32_t address);
uint32_t m68k_bus_read32(uint32_t address);
void     m68k_bus_write8(uint32_t address, uint8_t value);
void     m68k_bus_write16(uint32_t address, uint16_t value);
void     m68k_bus_write32(uint32_t address, uint32_t value);

#ifdef __cplusplus
}
#endif
#endif
