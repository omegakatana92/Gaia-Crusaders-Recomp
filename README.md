# Gaia Crusaders — native port (static recompilation)

A native Windows / Linux / Steam Deck build of Noise Factory's 1999 arcade
beat 'em up *Gaia Crusaders* (MAME set `gaia`), which runs on a Cave 68000
board. The 68000 game program is translated ahead of time into C, and the
board is reimplemented around it.

You need your own `gaia.zip` (10 files, MAME 0.264 set; no BIOS). No ROM data,
extracted graphics or sound is in this repository or in any public package.

## What this is (and is not)

| Technique | Used here? |
|---|---|
| **Static recompilation** — every 68000 instruction the analysis reaches (vectors, branch/call targets, jump and pointer tables, entry points found by running the game) is translated to a C function at build time (`tools/recompile68k.py`, using the arcade-decomp 68000 disassembler) and linked into the executable. | **Yes**, in the *recompiled* build. |
| **Decompilation** — readable, hand-named C of the game logic. | No. The generated C is mechanical, one function per instruction. |
| **Dynamic recompilation (JIT)** | No. Nothing is translated at run time. |
| **Interpretation** | A fallback 68000 interpreter (`runtime/src/m68k_interp.c`) whose decode table is generated from the *same* disassembler and timing model and which calls the *same* instruction helpers. In the recompiled build it only runs code the static analysis missed; the **generic** build uses it for everything. |
| **Hardware reimplementation** | Yes: three NEC 038 tilemaps (8×8/16×16, row scroll, row select, flip), the Cave 013 sprite generator with zoom and its Z-buffer mode, the xGRB-555 palette, the YMZ280B, and the Cave interrupt logic and timing. MAME 0.264 is the behavioural reference; no MAME code is linked. |

### Two kinds of build

| Build | Contains | Redistributable? |
|---|---|---|
| **Recompiled** (`-DGAIA_ROM=...`) | C translated from *your* ROM | **No — keep it private.** It is code derived from the copyrighted program ROM. |
| **Generic** (`-DGA_GENERIC_BUILD=ON`) | no ROM-derived code; the 68000 runs on the interpreter | Yes. Public release packages are generic builds. |

Both load your ROM at run time and produce identical output: per-frame RAM
hashes match over 5,000 frames of random play (122.7 million interpreted
instructions in the generic build). Headless, the generic build runs at
~560 fps and the recompiled build ~690 fps; the game needs 57.55.

## Status

| Area | Status |
|---|---|
| ROM loading and validation (size + CRC32 in the runtime, SHA-1 in `tools/verify_rom.py`; files may sit in a sub-folder of the zip; Thunder Heroes is recognised and refused) | implemented + tested |
| 68000 translation + fallback interpreter | implemented + tested (recompiled vs generic identical; runs reach no instruction the runtime cannot execute) |
| Boot, system check, title, attract demo | implemented + tested — **work RAM byte-identical and screen pixel-identical to MAME 0.264 at all 13 reference frames from 60 to 4,800** (≈83 s, through the gameplay demo and the score ranking) |
| Graphics (tilemaps, scroll effects, zoomed sprites, priorities) | implemented + tested against MAME snapshots |
| Audio (YMZ280B) | implemented + tested: the game's 7,422 YMZ280B register reads/writes over 2,600 frames are identical to MAME's in order and value; output level matches MAME's (uncompressed) recording to within 1 % in every second, envelope correlation 0.998 |
| Gameplay | tested with scripted/random inputs (headless); **not played by a person** |
| Save states, autosave, rewind, input movies | implemented + tested (600-frame headless round trip) |
| Game data persistence | none needed: Gaia has no EEPROM or NVRAM (its settings are DIP switches, saved by the frontend) |
| Gadgets | Score P1 / Score P2 (verified addresses), plus FPS, frame time, input display |
| Frontend | implemented; tested under Xvfb (Linux) and Wine; **controllers, Steam Deck and a Windows PC not tested** |
| Windows x64 packages, Linux / Steam Deck AppImage | built; **not run on Windows 10/11 hardware or a Steam Deck** |

## Quick start

1. Get the package for your platform (or build it, see below).
2. Start it, choose **ROM Selection** and pick your `gaia.zip` (keyboard,
   mouse or controller). Every file is checked.
3. **Start Game**. Coin = `5`, P1 Start = `Enter`, buttons 1–4 = `Z X C V`.
   `Esc` (or Back + Start on a controller) opens the quick menu.
   See [docs/CONTROLS.md](docs/CONTROLS.md).

## Building

```bash
git clone --recursive <this repo> && cd Gaia-Crusaders-Recomp
# recompiled build (translates your ROM at configure time; keep the result private)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAIA_ROM=/path/to/gaia.zip
cmake --build build
./build/bin/GaiaCrusaders
./build/bin/gaia_headless --rom /path/to/gaia.zip --frames 3000 --png shot.png

# generic build (no ROM needed to build)
cmake -S . -B build-generic -G Ninja -DCMAKE_BUILD_TYPE=Release -DGA_GENERIC_BUILD=ON
cmake --build build-generic
```

The CMake files ship in the source package attached to each release (they are
not kept in the git repository). `tools/core` is the arcade-decomp submodule.
Details: [docs/BUILD_LINUX.md](docs/BUILD_LINUX.md),
[docs/BUILD_WINDOWS.md](docs/BUILD_WINDOWS.md), [docs/STEAM_DECK.md](docs/STEAM_DECK.md).

## Documentation

- [docs/ROM_SETUP.md](docs/ROM_SETUP.md) — required files and checks
- [docs/CONTROLS.md](docs/CONTROLS.md) — mappings, DIP switches, service/test
- [docs/FRONTEND.md](docs/FRONTEND.md) — menus, display/audio options, profiles, gadgets
- [docs/SAVE_STATES.md](docs/SAVE_STATES.md) — format and guarantees
- [docs/HARDWARE.md](docs/HARDWARE.md) — the board, the translator, the interpreter and how they were verified
- [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md)
- [docs/MODIFIED_FILES.md](docs/MODIFIED_FILES.md) — what is shared with the other projects

## Licences

This project's own code is GPL-2.0 (`LICENSE`). Board behaviour follows MAME
(BSD-3-Clause), the 68000 disassembler is arcade-decomp (BSD-3-Clause); see
`LICENSES/`. SDL2 (zlib), miniz (MIT), Inter font (OFL). *Gaia Crusaders* is ©
Noise Factory; this project is not affiliated with or endorsed by its rights
holders.
