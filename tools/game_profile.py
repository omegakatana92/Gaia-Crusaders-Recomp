"""Game profile for the shared arcade-decomp core (tools/core) and our translator."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
import rom_set  # noqa: E402

rom = rom_set
GAME = "Gaia Crusaders"
ROM_SET_TITLE = "gaia"
CPU = "68000"
BINARY = "gaia"
REFERENCE = "gaia_ref"

CODE_START = 0x100
PROGRAM_SIZE = rom_set.PROGRAM_SIZE

# Script interpreters that run native code embedded in data (none known yet).
SCRIPT_INLINE_OPCODES = ()
