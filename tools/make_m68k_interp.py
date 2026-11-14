"""Generate runtime/src/m68k_interp_table.c: the decode table of the 68000
fallback interpreter (runtime/src/m68k_interp.c).

Every one of the 65,536 opcode words is decoded once, here, by the same
disassembler the static translator uses (tools/core/m68000/disassembler.py),
and charged the same static cycle cost (tools/m68k_timing.py).  The table
records, per opcode, the instruction class, size, condition, static cycles
and how each operand is formed (register numbers, quick values, and which
extension words supply displacements / addresses / immediates).  The C
interpreter reads the extension words at run time and calls the same
semantic helpers (runtime/src/m68k_ops.c) as the translated code, so an
instruction behaves the same on both paths.  No ROM data is involved.

usage: python tools/make_m68k_interp.py [--out runtime/src/m68k_interp_table.c]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "core"))

import m68k_timing  # noqa: E402
from m68000 import disassembler as dis  # noqa: E402

# Marker extension words: bits 8-10 clear (a valid 68000 brief index word),
# a distinct low byte and a non-zero high byte so that byte, word and long
# immediates are told apart.
MARKERS = [0xF891, 0xF8A2, 0xF8B3, 0xF8C4, 0xF8D5, 0xF8E6]

MNEMONICS = [
    "invalid", "nop", "reset", "move_usp_to", "move_usp_from", "move", "move_to_sr", "move_to_ccr",
    "move_from_sr", "move_from_ccr", "movea", "moveq", "lea", "pea", "add", "sub", "adda", "suba",
    "addi", "subi", "andi", "ori", "eori", "andi_ccr", "ori_ccr", "eori_ccr", "andi_sr", "ori_sr",
    "eori_sr", "addq", "subq", "addx", "subx", "cmp", "cmpa", "cmpi", "and", "or", "eor", "cmpm",
    "negx", "nbcd", "abcd", "sbcd", "movep_to_mem", "movep_to_reg", "clr", "tst", "neg", "not",
    "swap", "ext", "mulu", "muls", "divu", "divs", "shift_mem", "shift_imm", "shift_reg",
    "bit_imm", "bit_reg", "movem_to_mem", "movem_to_regs", "link", "unlk", "exg", "bra", "bsr",
    "bcc", "dbcc", "jmp", "jsr", "rts", "rte", "rtr", "trap", "stop",
]
MID = {m: i for i, m in enumerate(MNEMONICS)}

KINDS = ["none", "dreg", "areg", "ind", "post", "pre", "disp", "index", "abs_w", "abs_l", "pc_disp",
         "pc_index", "imm_b", "imm_w", "imm_l", "quick", "reglist"]
KID = {k: i for i, k in enumerate(KINDS)}
SHIFTS = {"asl": 0, "asr": 1, "lsl": 2, "lsr": 3, "rol": 4, "ror": 5, "roxl": 6, "roxr": 7}
BITOPS = {"btst": 0, "bchg": 1, "bclr": 2, "bset": 3}
SIZES = {None: 2, "b": 1, "w": 2, "l": 4}


def reg_number(name: str) -> int:
    return int(name[1:]) + (8 if name[0] == "A" else 0)


def decode(op: int):
    data = bytes([op >> 8, op & 0xFF]) + b"".join(bytes([m >> 8, m & 0xFF]) for m in MARKERS)
    return dis.decode(dis.FlatMemory(data), 0)


def operand(o) -> tuple[int, int, int, int]:
    """(kind, reg, value, ext words) for one operand of the marker decode."""
    m = o.mode
    if m == "dreg":
        return KID["dreg"], reg_number(o.reg), 0, 0
    if m == "areg":
        return KID["areg"], reg_number(o.reg) & 7, 0, 0
    if m in ("indirect", "postinc", "predec"):
        return KID[{"indirect": "ind", "postinc": "post", "predec": "pre"}[m]], reg_number(o.reg) & 7, 0, 0
    if m == "disp":
        return KID["disp"], reg_number(o.reg) & 7, 0, 1
    if m == "index":
        return KID["index"], reg_number(o.reg) & 7, 0, 1
    if m == "abs_short":
        return KID["abs_w"], 0, 0, 1
    if m == "abs_long":
        return KID["abs_l"], 0, 0, 2
    if m == "pc_disp":
        return KID["pc_disp"], 0, 0, 1
    if m == "pc_index":
        return KID["pc_index"], 0, 0, 1
    if m == "immediate":
        v = o.value & 0xFFFFFFFF
        lows = {mk & 0xFF for mk in MARKERS}
        # reg = 1 when the disassembler sign-extends this immediate
        if (v & 0xFF) in lows and (v >> 8) in (0, 0xFFFFFF):
            return KID["imm_b"], 1 if v >> 8 else 0, 0, 1
        if (v & 0xFFFF) in MARKERS and (v >> 16) in (0, 0xFFFF):
            return KID["imm_w"], 1 if v >> 16 else 0, 0, 1
        if v == (MARKERS[0] << 16 | MARKERS[1]) or v == (MARKERS[1] << 16 | MARKERS[2]):
            return KID["imm_l"], 0, 0, 2
        raise ValueError(f"immediate {v:#x}")
    if m == "quick":
        return KID["quick"], 0, o.value & 0xFFFFFFFF, 0
    if m == "reglist":
        return KID["reglist"], 0, 0, 1
    raise ValueError(m)


def classify(ins) -> tuple[str, int]:
    """(table class, extra) mirroring recompile68k._emit_instruction."""
    m = ins.mnemonic
    ops = ins.operands
    if m == "move" and len(ops) == 2 and ops[1].mode == "usp":
        return "move_usp_to", 0
    if m == "move" and len(ops) == 2 and ops[0].mode == "usp":
        return "move_usp_from", 0
    if m in ("nop", "reset"):
        return m, 0
    if m == "move":
        if ops[1].mode == "sr":
            return "move_to_sr", 0
        if ops[1].mode == "ccr":
            return "move_to_ccr", 0
        if ops[0].mode == "sr":
            return "move_from_sr", 0
        if ops[0].mode == "ccr":
            return "move_from_ccr", 0
        return "move", 0
    if m in ("movea", "moveq", "lea", "pea", "addq", "subq", "addx", "subx", "cmpa", "cmpi", "and",
             "or", "eor", "cmpm", "negx", "nbcd", "abcd", "sbcd", "clr", "tst", "neg", "not", "swap",
             "ext", "mulu", "muls", "divu", "divs", "link", "unlk", "exg", "bra", "bsr", "bcc", "dbcc",
             "jmp", "jsr", "rts", "rte", "rtr", "trap", "stop"):
        return m, 0
    if m in ("add", "sub"):
        if ops[1].mode == "areg":
            return "adda" if m == "add" else "suba", 0
        return m, 0
    if m in ("adda", "suba"):
        return m, 0
    if m == "cmp":
        return ("cmpa" if ops[1].mode == "areg" else "cmp"), 0
    if m in ("addi", "subi", "andi", "ori", "eori"):
        if ops[1].mode == "ccr":
            if m not in ("andi", "ori", "eori"):
                raise ValueError("to CCR")
            return m + "_ccr", 0
        if ops[1].mode == "sr":
            if m not in ("andi", "ori", "eori"):
                raise ValueError("to SR")
            return m + "_sr", 0
        return m, 0
    if m == "movep":
        return ("movep_to_mem" if ops[0].mode == "dreg" else "movep_to_reg"), 0
    if m in SHIFTS:
        if len(ops) == 1:
            return "shift_mem", SHIFTS[m]
        if ops[0].mode == "quick":
            return "shift_imm", SHIFTS[m]
        if ops[0].mode == "dreg":
            return "shift_reg", SHIFTS[m]
        raise ValueError("shift count")
    if m in BITOPS:
        if ops[0].mode in ("immediate", "quick"):
            return "bit_imm", BITOPS[m]
        if ops[0].mode == "dreg":
            return "bit_reg", BITOPS[m]
        raise ValueError("bit source")
    if m == "movem":
        return ("movem_to_mem" if ops[0].mode == "reglist" else "movem_to_regs"), 0
    raise ValueError(f"unsupported {m}")


def entry(op: int) -> tuple:
    try:
        ins = decode(op)
        cls, extra = classify(ins)
    except Exception:
        return (0,) * 12
    if ins.mnemonic in ("illegal",) or "INVALID" in ins.text or ins.text.startswith("dc."):
        return (0,) * 12
    ops = list(ins.operands)
    kinds = []
    for o in ops[:2]:
        try:
            kinds.append(operand(o))
        except ValueError:
            kinds.append((0, 0, 0, 0))
    while len(kinds) < 2:
        kinds.append((0, 0, 0, 0))
    # special operand shapes handled by the class itself
    if cls in ("move_usp_to",):
        kinds[1] = (0, 0, 0, 0)
    if cls in ("move_usp_from",):
        kinds[0] = (0, 0, 0, 0)
    if cls in ("move_to_sr", "move_to_ccr", "andi_ccr", "ori_ccr", "eori_ccr", "andi_sr", "ori_sr", "eori_sr"):
        kinds[1] = (0, 0, 0, 0)
    if cls in ("move_from_sr", "move_from_ccr"):
        kinds[0] = (0, 0, 0, 0)
    ext_words = sum(k[3] for k in kinds)
    if cls in ("bcc", "bra", "bsr"):
        ext_words = 0 if (op & 0xFF) else 1
    if cls == "dbcc":
        ext_words = 1
    if cls in ("bcc", "bra", "bsr", "dbcc"):
        kinds = [(0, 0, 0, 0), (0, 0, 0, 0)] if cls != "dbcc" else [operand(ops[0]), (0, 0, 0, 0)]
    if cls == "movep_to_mem" or cls == "movep_to_reg":
        ext_words = 1
    if 2 + 2 * ext_words != ins.length:
        raise SystemExit(f"opcode {op:04X} ({ins.text}): {ext_words} extension words vs length {ins.length}")
    cond = dis.CONDITIONS.index(ins.condition) if ins.condition else 0
    try:
        cycles = m68k_timing.cycles(ins)
    except Exception:
        cycles = 4
    if cls in ("movem_to_mem", "movem_to_regs"):
        reglist = ops[0] if cls == "movem_to_mem" else ops[1]
        n = len(reglist.registers)
        cycles -= n * (8 if ins.size == "l" else 4)          # the per-register part is added at run time
    if cls == "shift_reg":
        pass                                                   # + 2 * count at run time, as emitted
    size = SIZES[ins.size]
    a, b = kinds
    return (MID[cls], size, cond, cycles, extra, a[0], a[1], a[2] & 0xFFFFFFFF, b[0], b[1], b[2] & 0xFFFFFFFF,
            1 if ins.length == 2 else 0)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=ROOT / "runtime" / "src" / "m68k_interp_table.c")
    args = ap.parse_args()
    rows = [entry(op) for op in range(0x10000)]
    used = sum(1 for r in rows if r[0])
    L = ["/* Generated by tools/make_m68k_interp.py -- do not edit.",
         " * Per-opcode decode and static timing for the 68000 fallback interpreter,",
         " * produced by the translator's own disassembler and timing model. */",
         '#include "m68k/interp.h"', "",
         "const m68k_decoded m68k_decode_table[65536] = {"]
    for i in range(0, 65536, 4):
        L.append("    " + " ".join("{%d,%d,%d,%d,%d,{%d,%d,0x%Xu},{%d,%d,0x%Xu},%d}," % r for r in rows[i:i + 4]))
    L += ["};", ""]
    args.out.write_text("\n".join(L), encoding="ascii")
    hdr = ROOT / "runtime" / "include" / "m68k" / "interp_classes.h"
    H = ["/* Generated by tools/make_m68k_interp.py -- do not edit. */", "#ifndef M68K_INTERP_CLASSES_H",
         "#define M68K_INTERP_CLASSES_H", "enum {"]
    H += [f"    M68K_I_{m.upper()} = {i}," for i, m in enumerate(MNEMONICS)]
    H += ["};", "enum {"]
    H += [f"    M68K_O_{k.upper()} = {i}," for i, k in enumerate(KINDS)]
    H += ["};", "#endif", ""]
    hdr.write_text("\n".join(H), encoding="ascii")
    print(f"{args.out}: {used} valid opcodes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
