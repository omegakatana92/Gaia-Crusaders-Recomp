"""Verify the player's gaia.zip against MAME's metadata (CRC32 + SHA-1)."""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import rom_set


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    args = parser.parse_args()
    checks = rom_set.check_archive(args.rom)
    failed = 0
    for check in checks:
        print(f"{'OK  ' if check.ok else 'FAIL'} {check.name:<14} {check.detail}")
        failed += not check.ok
    ok = sum(1 for check in checks if check.ok)
    print(f"\n{ok}/{len(rom_set.ROM_FILES)} files verified")
    if failed:
        print("ROM set does not match MAME's gaia set.", file=sys.stderr)
        return 1
    print("ROM set matches MAME gaia (Gaia Crusaders).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
