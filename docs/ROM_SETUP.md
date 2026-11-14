# ROM setup

One MAME-format archive is needed: `gaia.zip`, MAME 0.264 set `gaia`. The Cave
board has no BIOS, CHD or samples.

| File | Size | CRC32 | Role |
|---|---|---|---|
| `prg1.127` | 512 KiB | `47b904b2` | 68000 program, even bytes |
| `prg2.128` | 512 KiB | `469b7794` | 68000 program, odd bytes |
| `obj1.736` | 4 MiB | `f4f84e5d` | sprites |
| `obj2.738` | 4 MiB | `15c2a9ce` | sprites |
| `bg1.989` | 4 MiB | `013a693d` | layer 0 tiles |
| `bg2.995` | 4 MiB | `783cc62f` | layer 1 tiles |
| `bg3.998` | 4 MiB | `bcd61d1c` | layer 2 tiles |
| `snd1.447` | 4 MiB | `92770a52` | YMZ280B samples |
| `snd2.454` | 4 MiB | `329ae1cf` | YMZ280B samples |
| `snd3.455` | 4 MiB | `4048d64e` | YMZ280B samples |

The files may be at the top of the archive or inside a folder (for example
`gaia/prg1.127`). Thunder Heroes (`theroes`), which uses the same board, is
recognised and refused.

## How the files are checked

- **Frontend / runtime:** the size and CRC32 of every file (from the ZIP
  directory) is checked before anything is loaded; missing or wrong files are
  named in the message. At start-up the frontend also looks for `gaia.zip` in
  the data folder, `~/Downloads` and `~/roms`.
- **Tools / build:** `python3 tools/verify_rom.py gaia.zip` checks size, CRC32
  and SHA-1 of every extracted file.

## What the build does with your ROM

A *recompiled* build reads the 68000 program at configure time and writes C
translations to `build/generated/`. That C is derived from your ROM: keep
`build/`, `dist/` and any recompiled package private and never commit them. A
*generic* build (`-DGA_GENERIC_BUILD=ON`) reads no ROM when it is built.
Graphics and samples are always read from your archive at run time; nothing is
extracted to disk.
