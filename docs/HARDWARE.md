# Hardware and approach

## The board (MAME 0.264 `atlus/cave.cpp`, `gaia_map`)

| Part | Details | Implementation |
|---|---|---|
| CPU | Motorola 68000, 16 MHz | translated program + fallback interpreter, `runtime/src/m68k_*.c` |
| Program | 1 MiB at 0x000000 (two byte-interleaved 512 KiB ROMs) | translated |
| Work RAM | 64 KiB at 0x100000 | `board.c` |
| Sprites | Cave 013: 64 KiB sprite RAM at 0x400000 (four 1,024-sprite banks), zoom, 4-bpp sprites with 256-colour palette granularity, Z-buffer mode when sprite priorities go out of order | `video.c` |
| Tilemaps | three NEC 038 at 0x500000/0x600000/0x700000: 8x8 or 16x16 tiles from 8-bpp 8x8 elements, row scroll and row select from line RAM, flip, per-layer priority | `video.c` |
| Palette | 32,768 xGRB-555 entries at 0xC00000 | `video.c` |
| Sound | Yamaha YMZ280B at 16 MHz (most Cave games use 16.9344 MHz), 12 MiB of samples, mono speaker | `ymz280b.c` (shared with the Cyvern project) |
| Inputs | 0xD00010 (players), 0xD00012 (coins, start, test), 0xD00014 (two DIP banks) | `board.c` |
| Interrupts | one level-1 line for VBLANK, the YMZ280B and an unused cause; the cause register at 0x800000 clears VBLANK on read | `board.c` |
| Screen | 320x224 visible of a 240-line frame, 15625/271.5 = 57.5506 Hz (17,376 µs) | frontend |

There is no EEPROM, NVRAM, sound CPU or protection.

## Timing (as MAME runs it)

- A frame is 278,016 CPU cycles (240 lines of 1,158.4).
- VBLANK, where the picture is drawn and `interrupt()` runs, is at the
  **240-line frame boundary** (MAME's old-style screen with a zero vblank time).
  Putting it after the 224 visible lines instead shifts every game access by
  16 lines (1,158 µs); with the boundary, the game's sound-chip accesses land
  within ±3 µs of MAME's.
- The VBLANK interrupt is raised 15,376 µs after VBLANK (2,000 µs before the
  next one); the sprite list is latched then. A second timer 2,000 µs later
  clears the bit the irq-cause register reports in bit 2.
- The YMZ280B shares the 16 MHz clock: one output sample per 192 CPU cycles.
  The chip is brought up to the CPU's exact time before every register access,
  as MAME updates its stream. A finished sample raises its status bit only at
  MAME's update points (a register access, or the 50 Hz sound-manager update)
  and after the instruction that triggered the update.

## Translator and interpreter

`tools/recompile68k.py` (from the Violent Storm project) decodes the program
with arcade-decomp's 68000 disassembler (`tools/core`) and emits one C function
per instruction, each charging the MC68000 cycle time from `tools/m68k_timing.py`
and calling the semantic helpers in `runtime/src/m68k_ops.c`.

`tools/make_m68k_interp.py` runs the same disassembler over all 65,536 opcode
words (with marker extension words that reveal which operands come from
extension words and whether immediates are sign-extended) and the same timing
model, and writes a decode table. `runtime/src/m68k_interp.c` reads extension
words at run time and calls the same helpers with the same effective-address
descriptors and cycle charges as the emitted code. 44,524 opcode words decode
to instructions the runtime models; Scc, TAS, CHK, TRAPV and line-A/F are not
modelled by either path (the game does not use them in any tested run).

## How it was verified

Against MAME 0.264 running the same ROM set (references made locally, not
distributed):

- **Screen:** pixel-identical at frames 60, 120, 300, 600, 900, 1200, 1500,
  1800, 2400, 3000, 3600, 4200 and 4800 (system check, title, attract demo with
  scrolling stages, sprites and HUD, score ranking).
- **Work RAM:** byte-identical at frames 60, 600, 1800, 3000 and 4800.
- **Sound chip:** the 7,422 YMZ280B reads and writes of the first 2,600 frames
  are identical to MAME's in order and value, at times within ±3 µs.
- **Audio output:** against MAME's `-wavwrite` recording made with
  `-nocompressor` (MAME's mixer otherwise turns loud passages down by up to
  1.8×), the level matches within 1 % in every second of 83 s and the
  loudness envelope correlates at 0.998. MAME's default (compressed) recording
  differs only in the loud gameplay-demo passages.
- **Translator vs interpreter:** recompiled and generic builds produce identical
  per-frame RAM hashes over 5,000 frames of random two-player input.
