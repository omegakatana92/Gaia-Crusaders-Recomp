# Shared code and what changed

| Path | Origin | Changes for Gaia Crusaders |
|---|---|---|
| `frontend/` | Cyvern `frontend/` (newest shared frontend: rotation support, Windows-safe data folder names) | unchanged |
| `tools/core` (submodule) | arcade-decomp, as in Violent Storm | unchanged |
| `tools/recompile68k.py`, `tools/m68k_timing.py`, `tools/game_profile.py`, `tools/verify_rom.py` | Violent Storm | ROM set / names only |
| `runtime/src/m68k_ops.c`, `runtime/include/m68k/m68k.h`, `program_api.h`, `board/bus.h` | Violent Storm | unchanged |
| `runtime/src/runner.c`, `runtime/include/m68k/runner.h` | Violent Storm | fallback to the interpreter for untranslated code, strict mode, fallback statistics |
| `runtime/src/ymz280b.c`, `runtime/include/ymz/ymz280b.h` | Cyvern `ymz280b.c` | made board-independent (own state struct and sample ROM pointer), MAME's IRQ/status semantics added; usable by any project |
| `runtime/src/state.c` | Violent Storm / Cyvern serializer | sections for the Cave board |
| `ports/headless/main.c` | Cyvern headless runner | Gaia board API, fallback report, `--strict` |
| `ports/sdl2/main.c` | Violent Storm / Cyvern adapter structure | new adapter: 4 buttons, MAME's DIP switches, score gadgets |
| `scripts/*`, `packaging/appimage/*` | Cyvern | renamed |
| `tools/iterate_seeds.py` | Cyvern | 68000 seed format |

New for this project: `runtime/src/board.c`, `video.c` (Cave 038 tilemaps and
013 sprites ported from MAME; the tile decoder started from the ESP Ra.De.
port), `m68k_interp.c`, `m68k_interp_table.c` (generated),
`m68k_program_none.c`, `runtime/include/ga/*`, `runtime/include/m68k/interp.h`,
`interp_classes.h` (generated), `tools/make_m68k_interp.py`, `tools/rom_set.py`.

The 68000 fallback interpreter is ROM-independent and can be dropped into the
Violent Storm project (or any project using `recompile68k.py`) to get the same
generic-build option.
