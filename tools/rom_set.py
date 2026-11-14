"""Gaia Crusaders ROM set definitions and loaders.

Metadata from MAME 0.264's `gaia` driver (src/mame/atlus/cave.cpp,
ROM_START(gaia)): Noise Factory 1999, Cave 68000 board with three 038
tilemaps, the 013 sprite generator and a YMZ280B.  Callers pass the user's
own archive; images are built in memory or under build/ only.
"""
from __future__ import annotations

import hashlib
import struct
import zipfile
from dataclasses import dataclass
from pathlib import Path

PROGRAM_SIZE = 0x100000          # 68000 ROM at 0x000000-0x0FFFFF
PROGRAM_AREAS = ((0x000000, 0x100000),)


@dataclass(frozen=True)
class RomFile:
    name: str
    size: int
    crc32: int
    sha1: str
    role: str


# ROM_START( gaia ) - Gaia Crusaders (Noise Factory, 1999)
ROM_FILES = (
    RomFile("prg1.127", 0x80000, 0x47B904B2, "58b9b55f59cf00f70b690a0371096e86f4d723c2", "68000 program (even)"),
    RomFile("prg2.128", 0x80000, 0x469B7794, "502f855c51005a866900b19c3a0a170d9ea02392", "68000 program (odd)"),
    RomFile("obj1.736", 0x400000, 0xF4F84E5D, "8f445dd7a5c8a996939c211e5aec5742121a6e7e", "sprites"),
    RomFile("obj2.738", 0x400000, 0x15C2A9CE, "631eb2968395be86ef2403733e7d4ec769a013b9", "sprites"),
    RomFile("bg1.989", 0x400000, 0x013A693D, "2cc5be6f47c13febed942e1c3167946efedc5f9b", "layer 0 tiles"),
    RomFile("bg2.995", 0x400000, 0x783CC62F, "8b6e4212688b53be5ecc29ff2d41fd43e7d0a420", "layer 1 tiles"),
    RomFile("bg3.998", 0x400000, 0xBCD61D1C, "660a3b02a8c39e1117b00d0ad06f73221fef4ce8", "layer 2 tiles"),
    RomFile("snd1.447", 0x400000, 0x92770A52, "81f6835e1b45eb0f367e4586fdda92466f02edb9", "YMZ280B samples"),
    RomFile("snd2.454", 0x400000, 0x329AE1CF, "0c5e5074a5d8f4fb85ab4893bc953f192dcb301a", "YMZ280B samples"),
    RomFile("snd3.455", 0x400000, 0x4048D64E, "5e4ec6d37e70484e2fcd04188385e79ef0b53026", "YMZ280B samples"),
)

# Thunder Heroes (theroes) runs on the same board but is a different game.
OTHER_REVISIONS = {"t-h.u127": "theroes (Thunder Heroes)"}


def sha1(data: bytes) -> str:
    return hashlib.sha1(data).hexdigest()


def crc32(data: bytes) -> int:
    return zipfile.crc32(data) & 0xFFFFFFFF


@dataclass
class Check:
    name: str
    ok: bool
    detail: str


def _names(archive: zipfile.ZipFile) -> dict[str, str]:
    """base name (lower case) -> member name; archives may keep files in a folder"""
    out = {}
    for info in archive.infolist():
        if not info.is_dir():
            out.setdefault(info.filename.replace("\\", "/").split("/")[-1].lower(), info.filename)
    return out


def check_archive(path: Path) -> list[Check]:
    checks: list[Check] = []
    try:
        archive = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as error:
        return [Check(Path(path).name, False, f"cannot open archive: {error}")]
    names = _names(archive)
    for required in ROM_FILES:
        member = names.get(required.name.lower())
        if member is None:
            checks.append(Check(required.name, False, "missing"))
            continue
        data = archive.read(member)
        if len(data) != required.size:
            checks.append(Check(required.name, False, f"size {len(data):#x}, expected {required.size:#x}"))
        elif crc32(data) != required.crc32:
            checks.append(Check(required.name, False, f"CRC32 {crc32(data):08x}, expected {required.crc32:08x}"))
        elif sha1(data) != required.sha1:
            checks.append(Check(required.name, False, f"SHA-1 {sha1(data)}, expected {required.sha1}"))
        else:
            checks.append(Check(required.name, True, f"{len(data):#x} CRC32 {required.crc32:08x}  {required.role}"))
    for name, revision in OTHER_REVISIONS.items():
        if name in names:
            checks.append(Check(name, False, f"belongs to {revision}; only gaia is supported"))
    return checks


def _read(archive: zipfile.ZipFile, name: str) -> bytes:
    member = _names(archive).get(name.lower())
    if member is None:
        raise ValueError(f"{name}: missing")
    data = archive.read(member)
    entry = next(item for item in ROM_FILES if item.name == name)
    if len(data) != entry.size or crc32(data) != entry.crc32:
        raise ValueError(f"{name}: not the verified gaia file")
    return data


def build_program(path: Path) -> bytes:
    """68000 image: ROM_LOAD16_BYTE prg1 at even, prg2 at odd bytes."""
    with zipfile.ZipFile(path) as archive:
        image = bytearray(PROGRAM_SIZE)
        image[0::2] = _read(archive, "prg1.127")
        image[1::2] = _read(archive, "prg2.128")
    return bytes(image)


def canonical_address(address: int) -> int | None:
    address &= 0xFFFFFF
    return address if address < PROGRAM_SIZE else None


def address_in_program(address: int) -> bool:
    return canonical_address(address) is not None


def load_set(path: Path) -> Path:
    return Path(path)


def read_vector(image: bytes, vector: int) -> int:
    return struct.unpack_from(">I", image, vector * 4)[0]
