"""Static translation of a 68000 arcade program (here: Violent Storm) into C.

The 68000 text is decoded once, here, by tools/core/m68000/disassembler.py.
Each instruction becomes a small C function that calls a semantic helper
(runtime/src/m68k_ops.c); the runtime never fetches or decodes an instruction.

Generated C is ROM-derived and is written only under build/generated/, which is
git-ignored.  Nothing here is redistributable.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(TOOLS / "core"))

import game_profile  # noqa: E402
import rom_set
import m68k_timing  # noqa: E402
from m68000 import disassembler as dis  # noqa: E402

# The core's Analysis module imports the game profile itself; keep its code
# range in step with ours before importing.
game_profile.CODE_START = 0x100
from m68000 import analysis as core_analysis  # noqa: E402

core_analysis.CODE_START = 0x100

CONDITIONS = {name: index for index, name in enumerate(dis.CONDITIONS)}
SIZES = {None: 2, "b": 1, "w": 2, "l": 4}
SHIFTS = {"asl": 0, "asr": 1, "lsl": 2, "lsr": 3, "rol": 4, "ror": 5,
          "roxl": 6, "roxr": 7}
BITOPS = {"btst": 0, "bchg": 1, "bclr": 2, "bset": 3}


def reg_number(name: str) -> int:
    kind = name[0]
    number = int(name[1:])
    return number + (8 if kind == "A" else 0)


def condition_number(name: str | None) -> int:
    if name is None:
        raise ValueError("conditional instruction without a condition")
    return CONDITIONS[name]


def operand_ea(op) -> str:
    mode = op.mode
    if mode == "dreg":
        return f"EA_DREG({reg_number(op.reg)})"
    if mode == "areg":
        return f"EA_AREG({reg_number(op.reg) & 7})"
    if mode == "indirect":
        return f"EA_IND({reg_number(op.reg) & 7})"
    if mode == "postinc":
        return f"EA_POST({reg_number(op.reg) & 7})"
    if mode == "predec":
        return f"EA_PRE({reg_number(op.reg) & 7})"
    if mode == "disp":
        return f"EA_DISP({reg_number(op.reg) & 7}, {op.disp})"
    if mode == "index":
        index = reg_number(op.index)
        long_index = 1 if op.index_size == "l" else 0
        return f"EA_INDEX({reg_number(op.reg) & 7}, {index}, {long_index}, {op.disp})"
    if mode in ("abs_short", "abs_long", "pc_disp"):
        return f"EA_ABS(0x{op.value & 0xFFFFFF:06X}u)"
    if mode == "pc_index":
        address = (op.base + op.disp) & 0xFFFFFF
        index = reg_number(op.index)
        long_index = 1 if op.index_size == "l" else 0
        return f"EA_ABS_INDEX(0x{address:06X}u, {index}, {long_index})"
    if mode in ("immediate", "quick"):
        return f"EA_IMM(0x{op.value & 0xFFFFFFFF:08X}u)"
    raise ValueError(f"unsupported effective address mode {mode}")


def sized(mnemonic: str, size: str | None) -> str:
    """A C identifier suffix for the helper, if the helper is size-specific."""
    return mnemonic


def emit_instruction(instruction, next_address: int) -> tuple[list[str], str]:
    """Return (statements, return-expression) for one instruction, charging
    its 68000 cycle time (tools/m68k_timing.py)."""
    statements, ret = _emit_instruction(instruction, next_address)
    base = m68k_timing.cycles(instruction)
    ops = instruction.operands
    if instruction.mnemonic in SHIFTS and len(ops) == 2 and ops[0].mode == "dreg":
        statements.insert(0, f"s->cycles += {base}u + 2u * (s->d[{reg_number(ops[0].reg)}] & 63u);")
    elif base:
        statements.insert(0, f"s->cycles += {base}u;")
    if instruction.mnemonic == "bcc" and condition_number(instruction.condition) not in (0, 1):
        not_taken = 8 if instruction.length == 2 else 12
        ret = ret.replace("? 0x", "? (s->cycles += 10u, 0x", 1).replace(" : 0x", ") : (s->cycles += %du, 0x" % not_taken, 1) + ")"
    elif instruction.mnemonic == "bcc":
        statements.insert(0, "s->cycles += %du;" % (10 if condition_number(instruction.condition) == 0
                                                       else (8 if instruction.length == 2 else 12)))
    return statements, ret


def _emit_instruction(instruction, next_address: int) -> tuple[list[str], str]:
    """Return (statements, return-expression) for one instruction."""
    mnemonic = instruction.mnemonic
    size = SIZES[instruction.size]
    ops = instruction.operands
    statements: list[str] = []

    if mnemonic == "move" and len(ops) == 2 and ops[1].mode == "usp":
        statements.append(f"m68k_move_to_usp(s, s->a[{reg_number(ops[0].reg) & 7}]);")
        return statements, f"0x{next_address:06X}u"
    if mnemonic == "move" and len(ops) == 2 and ops[0].mode == "usp":
        statements.append(f"s->a[{reg_number(ops[1].reg) & 7}] = m68k_move_from_usp(s);")
        return statements, f"0x{next_address:06X}u"

    if mnemonic in ("nop", "reset"):
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "move":
        if len(ops) == 1:
            # MOVEM is decoded as movem; a single operand here is not expected.
            raise ValueError(f"move with one operand: {instruction.text}")
        src, dst = ops
        if dst.mode == "sr":
            if src.mode in ("immediate", "quick"):
                statements.append(f"m68k_set_sr(s, 0x{src.value & 0xFFFF:04X}u);")
            else:
                statements.append(f"m68k_move_to_sr(s, {operand_ea(src)}, {size});")
            return statements, f"0x{next_address:06X}u"
        if dst.mode == "ccr":
            if src.mode in ("immediate", "quick"):
                statements.append(f"m68k_set_ccr(s, 0x{src.value & 0xFF:02X}u);")
            else:
                statements.append(f"m68k_move_to_ccr(s, {operand_ea(src)}, {size});")
            return statements, f"0x{next_address:06X}u"
        if src.mode == "sr":
            statements.append(f"m68k_move_from_sr(s, {operand_ea(dst)}, {size});")
            return statements, f"0x{next_address:06X}u"
        if src.mode == "ccr":
            statements.append(f"m68k_move_from_ccr(s, {operand_ea(dst)}, {size});")
            return statements, f"0x{next_address:06X}u"
        statements.append(f"m68k_move(s, {operand_ea(src)}, {operand_ea(dst)}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "movea":
        statements.append(f"m68k_movea(s, {operand_ea(ops[0])}, {reg_number(ops[1].reg) & 7}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "moveq":
        statements.append(f"m68k_moveq(s, (int32_t)0x{ops[0].value & 0xFFFFFFFF:08X}u, {reg_number(ops[1].reg)});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "lea":
        statements.append(f"m68k_lea(s, {operand_ea(ops[0])}, {reg_number(ops[1].reg) & 7});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "pea":
        statements.append(f"m68k_pea(s, {operand_ea(ops[0])});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic in ("add", "sub"):
        if ops[1].mode == "areg":
            helper = "m68k_adda" if mnemonic == "add" else "m68k_suba"
            statements.append(f"{helper}(s, {operand_ea(ops[0])}, {reg_number(ops[1].reg) & 7}, {size});")
        else:
            helper = "m68k_add" if mnemonic == "add" else "m68k_sub"
            statements.append(f"{helper}(s, {operand_ea(ops[0])}, {operand_ea(ops[1])}, {size}, 0);")
        return statements, f"0x{next_address:06X}u"

    if mnemonic in ("adda", "suba"):
        helper = "m68k_adda" if mnemonic == "adda" else "m68k_suba"
        statements.append(f"{helper}(s, {operand_ea(ops[0])}, {reg_number(ops[1].reg) & 7}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic in ("addi", "subi", "andi", "ori", "eori"):
        value = ops[0].value & 0xFFFFFFFF
        dst = ops[1]
        if dst.mode == "ccr":
            helper = {"andi": "m68k_andi_ccr", "ori": "m68k_ori_ccr", "eori": "m68k_eori_ccr"}.get(mnemonic)
            if helper is None:
                raise ValueError(f"{mnemonic} to CCR is not valid")
            statements.append(f"{helper}(s, 0x{value:08X}u);")
            return statements, f"0x{next_address:06X}u"
        if dst.mode == "sr":
            if mnemonic == "andi":
                statements.append(f"m68k_set_sr(s, s->sr & 0x{value:08X}u);")
            elif mnemonic == "ori":
                statements.append(f"m68k_set_sr(s, s->sr | 0x{value:08X}u);")
            elif mnemonic == "eori":
                statements.append(f"m68k_set_sr(s, s->sr ^ 0x{value:08X}u);")
            else:
                raise ValueError(f"{mnemonic} to SR is not valid")
            return statements, f"0x{next_address:06X}u"
        statements.append(f"m68k_{mnemonic}(s, 0x{value:08X}u, {operand_ea(dst)}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic in ("addq", "subq"):
        helper = "m68k_addq" if mnemonic == "addq" else "m68k_subq"
        statements.append(f"{helper}(s, 0x{ops[0].value & 0xFFFFFFFF:08X}u, {operand_ea(ops[1])}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic in ("addx", "subx"):
        helper = "m68k_addx" if mnemonic == "addx" else "m68k_subx"
        statements.append(f"{helper}(s, {operand_ea(ops[0])}, {reg_number(ops[1].reg)}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "cmp":
        if ops[1].mode == "areg":
            statements.append(f"m68k_cmpa(s, {operand_ea(ops[0])}, {reg_number(ops[1].reg) & 7}, {size});")
        else:
            statements.append(f"m68k_cmp(s, {operand_ea(ops[0])}, {operand_ea(ops[1])}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "cmpa":
        statements.append(f"m68k_cmpa(s, {operand_ea(ops[0])}, {reg_number(ops[1].reg) & 7}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "cmpi":
        statements.append(f"m68k_cmpi(s, 0x{ops[0].value & 0xFFFFFFFF:08X}u, {operand_ea(ops[1])}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic in ("and", "or", "eor"):
        statements.append(f"m68k_{mnemonic}(s, {operand_ea(ops[0])}, {operand_ea(ops[1])}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "cmpm":
        statements.append(f"m68k_cmp(s, {operand_ea(ops[0])}, {operand_ea(ops[1])}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "negx":
        statements.append(f"m68k_negx(s, {operand_ea(ops[0])}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "nbcd":
        statements.append(f"m68k_nbcd(s, {operand_ea(ops[0])});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic in ("abcd", "sbcd"):
        statements.append(f"m68k_{mnemonic}(s, {operand_ea(ops[0])}, {operand_ea(ops[1])});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "movep":
        if ops[0].mode == "dreg":
            statements.append(f"m68k_movep(s, {reg_number(ops[1].reg) & 7}, {ops[1].disp}, "
                              f"{reg_number(ops[0].reg)}, {size}, 1);")
        else:
            statements.append(f"m68k_movep(s, {reg_number(ops[0].reg) & 7}, {ops[0].disp}, "
                              f"{reg_number(ops[1].reg)}, {size}, 0);")
        return statements, f"0x{next_address:06X}u"

    if mnemonic in ("clr", "tst", "neg", "not"):
        statements.append(f"m68k_{mnemonic}(s, {operand_ea(ops[0])}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "swap":
        statements.append(f"m68k_swap(s, {reg_number(ops[0].reg)});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "ext":
        statements.append(f"m68k_ext(s, {reg_number(ops[0].reg)}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic in ("mulu", "muls", "divu", "divs"):
        statements.append(f"m68k_{mnemonic}(s, {operand_ea(ops[0])}, {reg_number(ops[1].reg)});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic in SHIFTS and len(ops) == 1:
        # memory shift/rotate: one word by one bit
        statements.append(f"m68k_shift(s, {SHIFTS[mnemonic]}, 1u, {operand_ea(ops[0])}, 2);")
        return statements, f"0x{next_address:06X}u"

    if mnemonic in SHIFTS:
        if ops[0].mode == "quick":
            count = f"0x{ops[0].value & 0xFFFFFFFF:08X}u"
        elif ops[0].mode == "dreg":
            count = f"(s->d[{reg_number(ops[0].reg)}] & 63u)"
        else:
            raise ValueError(f"unsupported shift count: {instruction.text}")
        statements.append(f"m68k_shift(s, {SHIFTS[mnemonic]}, {count}, {operand_ea(ops[1])}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic in BITOPS:
        bit = ops[0]
        if bit.mode in ("immediate", "quick"):
            bit_expr = f"0x{bit.value & 0xFFFFFFFF:08X}u"
        elif bit.mode == "dreg":
            bit_expr = f"s->d[{reg_number(bit.reg)}]"
        else:
            raise ValueError(f"unsupported bit source: {instruction.text}")
        statements.append(f"m68k_bitop(s, {BITOPS[mnemonic]}, {bit_expr}, {operand_ea(ops[1])});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "movem":
        if ops[0].mode == "reglist":
            mask = sum(1 << reg_number(name) for name in ops[0].registers)
            statements.append(f"m68k_movem_to_mem(s, 0x{mask:04X}u, {operand_ea(ops[1])}, {size});")
        else:
            mask = sum(1 << reg_number(name) for name in ops[1].registers)
            statements.append(f"m68k_movem_to_regs(s, 0x{mask:04X}u, {operand_ea(ops[0])}, {size});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "link":
        statements.append(f"m68k_link(s, {reg_number(ops[0].reg) & 7}, (int32_t)0x{ops[1].value & 0xFFFFFFFF:08X}u);")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "unlk":
        statements.append(f"m68k_unlk(s, {reg_number(ops[0].reg) & 7});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "exg":
        first, second = ops
        both_data = first.mode == "dreg" and second.mode == "dreg"
        statements.append(f"m68k_exg(s, {reg_number(first.reg)}, {reg_number(second.reg)}, {1 if both_data else 0});")
        return statements, f"0x{next_address:06X}u"

    if mnemonic == "bra":
        return statements, f"0x{instruction.target & 0xFFFFFF:06X}u"

    if mnemonic == "bsr":
        statements.append(f"m68k_push32(s, 0x{next_address:06X}u);")
        return statements, f"0x{instruction.target & 0xFFFFFF:06X}u"

    if mnemonic == "bcc":
        cond = condition_number(instruction.condition)
        if cond == 0:
            return statements, f"0x{instruction.target & 0xFFFFFF:06X}u"
        if cond == 1:
            return statements, f"0x{next_address:06X}u"
        return statements, (f"m68k_cond(s, {cond}) ? 0x{instruction.target & 0xFFFFFF:06X}u "
                            f": 0x{next_address:06X}u")

    if mnemonic == "dbcc":
        cond = condition_number(instruction.condition)
        return statements, (f"m68k_dbcc(s, {reg_number(ops[0].reg)}, {cond}, "
                            f"0x{instruction.target & 0xFFFFFF:06X}u) "
                            f"? 0x{instruction.target & 0xFFFFFF:06X}u : 0x{next_address:06X}u")

    if mnemonic == "jmp":
        if instruction.target is not None:
            return statements, f"0x{instruction.target & 0xFFFFFF:06X}u"
        return statements, f"m68k_ea_address(s, {operand_ea(ops[0])})"

    if mnemonic == "jsr":
        statements.append(f"m68k_push32(s, 0x{next_address:06X}u);")
        if instruction.target is not None:
            return statements, f"0x{instruction.target & 0xFFFFFF:06X}u"
        return statements, f"m68k_ea_address(s, {operand_ea(ops[0])})"

    if mnemonic == "rts":
        return statements, "m68k_pop32(s)"
    if mnemonic == "rte":
        statements.append("m68k_rte(s);")
        return statements, "s->pc"
    if mnemonic == "rtr":
        statements.append("m68k_rtr(s);")
        return statements, "s->pc"

    if mnemonic == "trap":
        vector = ops[0].value & 15
        statements.append(f"m68k_trap(s, {vector});")
        return statements, "s->pc"

    if mnemonic == "stop":
        statements.append(f"m68k_stop(s, 0x{ops[0].value & 0xFFFF:04X}u);")
        return statements, f"0x{next_address:06X}u"

    raise ValueError(f"unsupported instruction at {instruction.address:06X}: {instruction.text}")


def branch_table_entries(image: bytes, minimum_run: int = 3) -> list[int]:
    """Entries of computed-jump tables: runs of `bra.w` (stride 4) or
    `jmp abs.l` (stride 6).  The game dispatches through these with
    `jsr (An,Dn.w)` / `jmp (d8,PC,Dn)`, which static flow cannot follow.
    Only addresses are derived; every entry is still trial-decoded."""
    entries: set[int] = set()
    size = len(image)

    def word(a: int) -> int:
        return (image[a] << 8) | image[a + 1]

    for opcode, stride in ((0x6000, 4), (0x4EF9, 6)):
        address = 0x100
        while address + stride * minimum_run <= size:
            run = 0
            while address + stride * (run + 1) <= size and word(address + stride * run) == opcode:
                run += 1
            if run >= minimum_run:
                for k in range(run):
                    entries.add(address + stride * k)
                address += stride * run
            else:
                address += 2
    return sorted(entries)


def relative_table_entries(image: bytes) -> list[int]:
    """Targets of PC-relative word-offset jump tables:
        move.w (d8,PC,Xn.w), Dk ; jmp|jsr (d8,PC,Dk.w)
    The table holds 16-bit offsets from the jump base.  Entries are read
    until the table would run into the first target."""
    found: set[int] = set()
    size = len(image)

    def word(a: int) -> int:
        return (image[a] << 8) | image[a + 1]

    def s8(v: int) -> int:
        return v - 0x100 if v & 0x80 else v

    def s16(v: int) -> int:
        return v - 0x10000 if v & 0x8000 else v

    for a in range(0x100, size - 8, 2):
        w = word(a)
        if (w & 0xF1FF) != 0x303B:
            continue
        k = (w >> 9) & 7
        jw = word(a + 4)
        if jw not in (0x4EFB, 0x4EBB):
            continue
        ext1, ext2 = word(a + 2), word(a + 6)
        if (ext2 >> 12) & 0xF != k or ext2 & 0x0800:   # index must be Dk.w
            continue
        table = a + 2 + s8(ext1 & 0xFF)
        base = a + 6 + s8(ext2 & 0xFF)
        if table < 0 or table >= size:
            continue
        limit = None
        for i in range(256):
            entry = table + 2 * i
            if entry + 2 > size or (limit is not None and entry >= limit):
                break
            target = base + s16(word(entry))
            if target < 0x100 or target >= size or target & 1:
                break
            found.add(target)
            if target > table and (limit is None or target < limit):
                limit = target
    return sorted(found)


def two_level_table_entries(image: bytes) -> list[int]:
    """Targets of two-level PC-relative dispatch tables (Konami object state
    machines), e.g. at 00B7B4:
        move.w (d,PC,D0.w),D0   ; group offset from the table base
        add.w  (4,A0),D0        ; + state index
        move.w (d,PC,D0.w),D0   ; handler offset from the table base
        jsr    (d,PC,D0.w)
    The table base holds group offsets that point at sub-tables inside the
    same table; each sub-table holds handler offsets from the base.  Found by
    a jmp/jsr (d8,PC,Dk.w) whose preceding move.w (d8,PC,Dk.w) reads the same
    base (a single-level table is handled by relative_table_entries)."""
    found: set[int] = set()
    size = len(image)

    def word(a: int) -> int:
        return (image[a] << 8) | image[a + 1]

    def s8(v: int) -> int:
        return v - 0x100 if v & 0x80 else v

    for j in range(0x104, size - 4, 2):
        jw = word(j)
        if jw not in (0x4EFB, 0x4EBB):
            continue
        ext = word(j + 2)
        if ext & 0x8800:            # index must be a data register, word size
            continue
        k = (ext >> 12) & 7
        base = j + 2 + s8(ext & 0xFF)
        mv = j - 4
        if mv < 0x100 or (word(mv) & 0xF1FF) != 0x303B or ((word(mv) >> 9) & 7) != k:
            continue
        if mv + 2 + s8(word(mv + 2) & 0xFF) != base:
            continue
        first = word(base)
        if first == 0 or first & 1 or first > 0x400:
            continue
        # first level: group offsets, ending at the first value that cannot be
        # a forward offset into the table (handler code may follow directly)
        groups_list = []
        for i in range(first // 2):
            w = word(base + 2 * i)
            if w == 0 or w & 1 or w >= 0x4000 or w <= 2 * i + 2 or base + w >= size:
                break
            groups_list.append(base + w)
        if not groups_list:
            continue
        level1_end = base + 2 * len(groups_list)
        groups = sorted(set(groups_list))
        if groups[0] < level1_end:
            continue
        gset = set(groups)
        leaves: set[int] = set()
        for gi, g in enumerate(groups):
            stop = groups[gi + 1] if gi + 1 < len(groups) else g + 0x200
            a = g
            while a < stop and a + 2 <= size and a not in leaves:
                w = word(a)
                t = base + (w - 0x10000 if w & 0x8000 else w)
                if w == 0 or w & 1 or t < 0x100 or t >= size or t in gset:
                    break
                leaves.add(t)
                a += 2
        # a sub-table ends where code begins: drop entries read past the
        # nearest handler that follows each sub-table
        for gi, g in enumerate(groups):
            stop = groups[gi + 1] if gi + 1 < len(groups) else g + 0x200
            nearest = min((t for t in leaves if t > g), default=stop)
            a = g
            while a < min(stop, nearest) and a + 2 <= size:
                w = word(a)
                t = base + (w - 0x10000 if w & 0x8000 else w)
                if w == 0 or w & 1 or t < 0x100 or t >= size or t in gset:
                    break
                found.add(t)
                a += 2
    return sorted(found)


def pointer_table_entries(image: bytes, code_end: int = 0x140000, minimum_run: int = 3) -> list[int]:
    """Targets of absolute-long code pointer tables addressed as
        movea.l (d8,PC,Xn), An ; jmp|jsr (An)
    Candidates are trial-decoded by the caller."""
    found: set[int] = set()
    size = len(image)

    def long(a: int) -> int:
        return int.from_bytes(image[a:a + 4], "big")

    def word(a: int) -> int:
        return (image[a] << 8) | image[a + 1]

    def plausible(v: int) -> bool:
        return 0x100 <= v < code_end and not (v & 1)

    for a in range(0x100, size - 6, 2):
        w = word(a)
        if (w & 0xF1FF) != 0x207B:            # movea.l (d8,PC,Xn), An
            continue
        n = (w >> 9) & 7
        jw = word(a + 4)
        if jw not in (0x4ED0 | n, 0x4E90 | n):  # jmp (An) / jsr (An)
            continue
        ext = word(a + 2)
        disp = (ext & 0xFF) - 0x100 if ext & 0x80 else ext & 0xFF
        table = a + 2 + disp
        for i in range(64):
            entry = table + 4 * i
            if entry + 4 > size or not plausible(long(entry)):
                break
            found.add(long(entry))
    return sorted(found)


def inline_script_entries(image: bytes) -> list[int]:
    """Candidate entry points of native code embedded in script streams: the
    address after each opcode word listed in game_profile.SCRIPT_INLINE_OPCODES.
    Candidates are trial-decoded by the caller; false positives only add
    unreachable translated functions."""
    opcodes = set(getattr(game_profile, "SCRIPT_INLINE_OPCODES", ()))
    found = []
    for address in range(0x102, len(image) - 2, 2):
        if ((image[address - 2] << 8) | image[address - 1]) in opcodes:
            found.append(address)
    return found


def collect_analysis(image: bytes, seeds: list[int]):
    analysis = core_analysis.Analysis(image, {})
    reset_pc = rom_set.read_vector(image, 1)
    analysis.enter(reset_pc, kind="func")
    for vector in range(1, 0x49):
        target = rom_set.read_vector(image, vector) & 0xFFFFFF
        if 0x100 <= target < rom_set.PROGRAM_SIZE:
            analysis.enter(target, kind="func")
    for seed in seeds:
        if 0x100 <= seed < rom_set.PROGRAM_SIZE and seed % 2 == 0 and analysis.clean(seed):
            analysis.enter(seed, kind="func")
    for seed in branch_table_entries(image):
        if analysis.clean(seed):
            analysis.enter(seed, kind="func")
    for seed in (inline_script_entries(image) + relative_table_entries(image) +
                 two_level_table_entries(image) +
                 pointer_table_entries(image)):
        if analysis.clean(seed):
            analysis.enter(seed, kind="func")
    analysis.run()
    return analysis


def load_seeds(path: Path) -> list[int]:
    seeds = []
    if not path.exists():
        return seeds
    for line in path.read_text(encoding="ascii").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 2 or parts[0] not in ("func", "code"):
            raise SystemExit(f"{path}: bad seed line: {line}")
        seeds.append(int(parts[1], 16) & 0xFFFFFF)
    return seeds


def generate(image: bytes, seeds: list[int], out_dir: Path, per_file: int = 900) -> dict:
    analysis = collect_analysis(image, seeds)
    out_dir.mkdir(parents=True, exist_ok=True)
    written: set[Path] = set()

    def write_if_changed(path: Path, text: str) -> None:
        # unchanged files keep their timestamps, so a rebuild after adding a
        # seed recompiles only the address blocks that changed
        written.add(path)
        if path.exists() and path.read_text(encoding="ascii") == text:
            return
        path.write_text(text, encoding="ascii")

    addresses = sorted(analysis.instructions)
    unsupported = []
    bodies: dict[int, tuple[str, str]] = {}
    for address in addresses:
        instruction = analysis.instructions[address]
        next_address = address + instruction.length
        try:
            statements, result = emit_instruction(instruction, next_address)
        except ValueError as error:
            # Reached only by heuristic roots that decode into instructions the
            # runtime does not model.  The block stops with a clear report
            # instead of executing anything if it is ever entered.
            unsupported.append(str(error))
            statements, result = [], f"m68k_unsupported(s, 0x{address:06X}u)"
        bodies[address] = (instruction.text, statements, result)
        continue
        bodies[address] = (instruction.text, statements, result)
    if unsupported:
        print(f"note: {len(unsupported)} unmodelled instructions in heuristic roots "
              "(trapped at run time)", file=sys.stderr)

    chunks = []
    blocks: dict[int, list[int]] = {}
    for address in addresses:                       # one file per 8 KiB of program
        blocks.setdefault(address >> 13, []).append(address)
    for block, chunk_addresses in sorted(blocks.items()):
        lines = [
            '/* Generated by tools/recompile68k.py from the user-supplied ROM.',
            ' * ROM-derived code: never commit or redistribute this file. */',
            '#include "m68k/m68k.h"',
            '#include "board/bus.h"',
            '',
        ]
        for address in chunk_addresses:
            text, statements, result = bodies[address]
            lines.append(f"/* {address:06X}: {text} */")
            lines.append(f"uint32_t b_{address:06X}(m68k_state *s)")
            lines.append("{")
            for statement in statements:
                lines.append(f"    {statement}")
            lines.append(f"    return {result};")
            lines.append("}")
            lines.append("")
        chunk = out_dir / f"program_{block << 13:06X}.c"
        write_if_changed(chunk, "\n".join(lines) + "\n")
        chunks.append(chunk)

    dispatch = [
        '/* Generated dispatch table for the translated program. */',
        '#include "m68k/m68k.h"',
        '#include <stdint.h>',
        '#include <stddef.h>',
        '',
    ]
    for address in addresses:
        dispatch.append(f"uint32_t b_{address:06X}(m68k_state *s);")
    dispatch += ['', 'typedef struct { uint32_t key; uint32_t (*fn)(m68k_state *); } m68k_entry;',
                 '', 'static const m68k_entry entries[] = {']
    for address in addresses:
        dispatch.append(f"    {{ 0x{address:06X}u, b_{address:06X} }},")
    dispatch += ['};', '', f'#define ENTRY_COUNT {len(addresses)}u',
                 f'#define HASH_BITS {max(17, (2 * len(addresses)).bit_length())}u', '#define HASH_SIZE (1u << HASH_BITS)',
                 'static uint32_t keys[HASH_SIZE];',
                 'static uint32_t slots[HASH_SIZE];',
                 'static int ready;',
                 '',
                 'static uint32_t hash_pc(uint32_t pc) {',
                 '    pc &= 0xFFFFFEu;',
                 '    pc ^= pc >> 17; pc *= 0x9E3779B1u; pc ^= pc >> 13;',
                 '    return (pc >> 1) & (HASH_SIZE - 1u);',
                 '}',
                 '',
                 'void m68k_program_init(void) {',
                 '    if (ready) return;',
                 '    for (unsigned i = 0; i < ENTRY_COUNT; i++) {',
                 '        uint32_t slot = hash_pc(entries[i].key);',
                 '        while (keys[slot]) slot = (slot + 1u) & (HASH_SIZE - 1u);',
                 '        keys[slot] = entries[i].key;',
                 '        slots[slot] = i;',
                 '    }',
                 '    ready = 1;',
                 '}',
                 '',
                 'uint32_t m68k_program_step(m68k_state *s, uint32_t pc) {',
                 '    uint32_t slot = hash_pc(pc);',
                 '    while (keys[slot]) {',
                 '        if (keys[slot] == pc) return entries[slots[slot]].fn(s);',
                 '        slot = (slot + 1u) & (HASH_SIZE - 1u);',
                 '    }',
                 '    return 0xFFFFFFFFu;',
                 '}',
                 '',
                 'int m68k_program_has(uint32_t pc) {',
                 '    m68k_program_init();',
                 '    uint32_t slot = hash_pc(pc);',
                 '    while (keys[slot]) {',
                 '        if (keys[slot] == pc) return 1;',
                 '        slot = (slot + 1u) & (HASH_SIZE - 1u);',
                 '    }',
                 '    return 0;',
                 '}',
                 '',
                 'uint32_t m68k_program_entry_count(void) { return ENTRY_COUNT; }',
                 'uint32_t m68k_program_missing(uint32_t pc) { (void)pc; return 0; }',
                 '']
    write_if_changed(out_dir / "program_dispatch.c", "\n".join(dispatch) + "\n")

    header = [
        '#ifndef VS_PROGRAM_H',
        '#define VS_PROGRAM_H',
        '#include "m68k/program_api.h"',
        '#endif',
        '',
    ]
    write_if_changed(out_dir / "program.h", "\n".join(header))
    for old in list(out_dir.glob("program_*.c")) + list(out_dir.glob("program_*.h")):
        if old not in written:
            old.unlink()

    return {
        "instructions": len(addresses),
        "functions": len(analysis.functions),
        "labels": len(analysis.labels),
        "chunks": len(chunks),
        "bytes": sum(len(c.read_bytes()) for c in chunks),
        "range": (min(addresses), max(addresses)),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path, nargs="?",
                        default=None)
    parser.add_argument("--seeds", type=Path, default=ROOT / "tools" / "seeds.txt")
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "generated")
    args = parser.parse_args()

    image = rom_set.build_program(args.rom)
    seeds = load_seeds(args.seeds)
    stats = generate(image, seeds, args.out)
    print(f"program image: {len(image):#x} bytes, reset PC {rom_set.read_vector(image, 1):06X}")
    print(f"instructions: {stats['instructions']}, functions: {stats['functions']}, "
          f"labels: {stats['labels']}")
    print(f"range: {stats['range'][0]:06X}-{stats['range'][1]:06X}; "
          f"{stats['chunks']} C files, {stats['bytes']} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
