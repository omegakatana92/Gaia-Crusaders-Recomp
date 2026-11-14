# Save states

Save states capture the complete running board and restore it exactly. Gaia
Crusaders has no EEPROM or battery RAM, so there is no separate game memory to
keep; the DIP switch settings live in the frontend's `settings.ini`.

## Using them

- **Slots 1-10** plus an **Autosave** slot. Save/Load State in either menu shows
  each slot's thumbnail, date and time, and play length.
- **Quick save / quick load** (F5 / F9 by default) use the current slot, shown
  with `*` in the slot list; F6 / F7 change it.
- Saving over an existing slot asks for confirmation in the menus. The quick-save
  hotkey overwrites the current slot without asking (that is what it is for).
- Loading while a game is running asks first, because unsaved progress is lost.
- **Autosave** is written when you return to the main menu or exit (both can be
  turned off in Extras). **Continue** on the main menu resumes it.
- Delete a slot with Delete / X in the slot list.

## Where they are stored

`<data folder>/states/gaia-<ROM fingerprint>/slotNN.fest` and
`autosave.fest`. The data folder is:

- Windows: `%APPDATA%\OmegaKatana\Gaia Crusaders\`
- Linux / Steam Deck: `~/.local/share/OmegaKatana/Gaia Crusaders/`
- Portable mode (a `portable.txt` beside the executable, or `--portable`):
  the executable's folder. Ignored inside an AppImage, which is read-only.

Because the folder name contains the ROM fingerprint, states from a different ROM
revision are never listed together with yours.

## What a state contains

The file is a container written atomically (write to `.tmp`, then rename):

| Field | Purpose |
| --- | --- |
| `FESTATE1`, container version | identifies the file format |
| game id `gaia`, runtime version | refuses states from other games |
| ROM fingerprint (CRC32 of program, sprite, tile and sample images) | refuses other revisions or a modified set |
| timestamp, frame count | shown in the slot list |
| 160x112 thumbnail | preview |
| core state | the board, see below |
| CRC32 of the whole file | detects damage |

The core state (`runtime/src/state.c`, magic `GAIACORE`) is a versioned, tagged,
little-endian blob with its own CRC32 and fingerprint. One routine walks the board
for both saving and loading, so the two layouts cannot drift apart. Sections:

- `M68K` - D0-D7, A0-A7, the inactive stack pointer, PC, SR, STOP flag,
  interrupt latch, instruction/exception/cycle counters.
- `RAM ` - 64 KiB work RAM.
- `VID ` - sprite RAM, the three 038 tilemap RAMs (tile maps, line RAM and the
  extra RAM after them), video and layer registers, palette RAM and computed
  pens, the latched 8x8/16x16 mode, the sprite list latched at the VBLANK
  interrupt, the sprite Z-buffer and its base value, and the last picture.
- `IRQ ` - the VBLANK, sound and "unknown" interrupt causes.
- `YMZ ` - YMZ280B registers and all eight voices.
- `MISC` - DIP switches, scanline/frame counters, the pending VBLANK and sound
  update timers, the audio sample counter.

No ROM data is stored in a state.

## Restoring safely

Loading first validates the whole file (magic, version, CRC, game id,
fingerprint, every section's tag and length). Only then is the running board
replaced. A damaged or incompatible state is rejected with a message and the
current game continues untouched. After loading, queued audio is flushed, held
inputs are cleared and must be released before they count again, and the
picture is re-rendered from the restored video state.

Because code the build did not translate runs on the fallback interpreter, a
state can be resumed anywhere: states move freely between the recompiled and
the generic build of the same ROM set, and between versions with different
translation coverage.

## Verification (2026-11-13)

- `gaia_headless --inputs random:1 --state-test 2400 --state-test-len 600`
  saves at frame 2400, runs 600 frames recording work RAM, sprite
  RAM, the composed picture and the audio output hash per frame, checks that a
  corrupted copy and a truncated copy are rejected and leave the board
  untouched, re-saves right after loading (byte-identical to the original
  state), then replays the same 600 frames. Result: **PASS, identical**.
  Registered as the `save_state_round_trip` CTest.
- In the SDL frontend (Xvfb): autosave written when leaving the game.

## Rewind

Extras > Rewind keeps a ring of compressed core states (every 4th frame) within a
memory budget (Rewind Memory Limit, default 64 MB; oldest history is dropped).
Hold the Rewind hotkey to step back. It uses the same serializer as save states.

## Input movies

Extras > Record Input Movie stores a full state plus one input word per frame
(`movies/movie-*.femv`); Play Latest Movie loads the state and replays the
inputs. Playback is exact because the runtime is deterministic (the development build,
the Linux release builds and the Windows builds under Wine, recompiled and
generic, produce identical per-frame RAM hashes over 5,000 frames of random
play).
