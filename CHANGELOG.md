# Changelog

## 0.1.0 — 2026-11-13

First release.

- 68000 static translation of the game program, with a fallback 68000
  interpreter generated from the same disassembler, timing model and helpers.
- Recompiled build (private, from your ROM) and generic build
  (redistributable, interpreter only), identical in output.
- Cave board: three 038 tilemaps, 013 sprite generator with zoom and
  Z-buffer mode, palette, YMZ280B, interrupt logic and timing as MAME runs it.
- Verified against MAME 0.264: screen pixel-identical and work RAM
  byte-identical at all reference frames up to 4,800; identical sound-chip
  register traffic; audio level within 1 %.
- Shared SDL2 frontend: menus, ROM picker, save states, rewind, movies, DIP
  switches, score gadgets.
- Windows x64 zips and a Steam Deck / Linux AppImage.
