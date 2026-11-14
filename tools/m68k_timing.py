"""MC68000 instruction timing (cycles), from the MC68000 User's Manual
section 8 tables.  Used by recompile68k.py to charge each translated
instruction its bus/execution time so the native program keeps the same
pace against video and interrupts as the original CPU.

The static part of each instruction is returned here.  Parts that depend on
run-time values are added by the runtime helpers instead:
  * Bcc taken/not taken and DBcc outcomes   (emitted inline / m68k_dbcc)
  * register shift counts (2 cycles per bit) (emitted inline)
  * MULU/MULS/DIVU/DIVS data-dependent time  (m68k_mul*/m68k_div*)
"""
from __future__ import annotations

# effective address calculation time: (byte/word, long)
EA = {
    "dreg": (0, 0), "areg": (0, 0), "indirect": (4, 8), "postinc": (4, 8),
    "predec": (6, 10), "disp": (8, 12), "index": (10, 14), "abs_short": (8, 12),
    "abs_long": (12, 16), "pc_disp": (8, 12), "pc_index": (10, 14),
    "immediate": (4, 8), "quick": (0, 0),
}
# destination write time for MOVE: (byte/word, long)
MOVE_DST = {
    "dreg": (0, 0), "areg": (0, 0), "indirect": (4, 8), "postinc": (4, 8), "predec": (4, 8),
    "disp": (8, 12), "index": (10, 14), "abs_short": (8, 12), "abs_long": (12, 16),
}
LEA = {"indirect": 4, "disp": 8, "index": 12, "abs_short": 8, "abs_long": 12, "pc_disp": 8, "pc_index": 12}
PEA = {"indirect": 12, "disp": 16, "index": 20, "abs_short": 16, "abs_long": 20, "pc_disp": 16, "pc_index": 20}
JMP = {"indirect": 8, "disp": 10, "index": 14, "abs_short": 10, "abs_long": 12, "pc_disp": 10, "pc_index": 14}
JSR = {"indirect": 16, "disp": 18, "index": 22, "abs_short": 18, "abs_long": 20, "pc_disp": 18, "pc_index": 22}
MOVEM_TO_MEM = {"indirect": 8, "predec": 8, "disp": 12, "index": 14, "abs_short": 12, "abs_long": 16}
MOVEM_TO_REG = {"indirect": 12, "postinc": 12, "disp": 16, "index": 18, "abs_short": 16, "abs_long": 20,
                "pc_disp": 16, "pc_index": 18}


def mode(op) -> str:
    return op.mode


def ea(op, long: bool) -> int:
    return EA.get(mode(op), (0, 0))[1 if long else 0]


def is_reg(op) -> bool:
    return op.mode in ("dreg", "areg")


def cycles(instruction) -> int:
    """Static cycle cost of one decoded instruction (see module notes)."""
    m = instruction.mnemonic
    size = instruction.size
    long = size == "l"
    ops = instruction.operands

    if m in ("nop",):
        return 4
    if m == "reset":
        return 132
    if m == "move" and len(ops) == 2 and (ops[0].mode == "usp" or ops[1].mode == "usp"):
        return 4
    if m == "move":
        src, dst = ops
        if dst.mode == "sr" or dst.mode == "ccr":
            return 12 + ea(src, False)
        if src.mode == "sr":
            return 6 if dst.mode == "dreg" else 8 + ea(dst, False)
        if src.mode == "ccr":
            return 6 if dst.mode == "dreg" else 8 + ea(dst, False)
        return 4 + ea(src, long) + MOVE_DST.get(mode(dst), (0, 0))[1 if long else 0]
    if m == "movea":
        return 4 + ea(ops[0], long)
    if m == "moveq":
        return 4
    if m == "lea":
        return LEA.get(mode(ops[0]), 4)
    if m == "pea":
        return PEA.get(mode(ops[0]), 12)
    if m in ("add", "sub", "and", "or", "eor", "cmp") and ops[1].mode == "areg":
        m = "adda" if m == "add" else "suba" if m == "sub" else "cmpa"
    if m in ("adda", "suba"):
        if long:
            return 6 + ea(ops[0], True) + (2 if ops[0].mode in ("dreg", "areg", "immediate") else 0)
        return 8 + ea(ops[0], False)
    if m == "cmpa":
        return 6 + ea(ops[0], long)
    if m in ("add", "sub", "and", "or", "cmp", "eor"):
        src, dst = ops
        if dst.mode == "dreg":
            if m == "cmp":
                return (6 if long else 4) + ea(src, long)
            if long:
                return 6 + ea(src, True) + (2 if src.mode in ("dreg", "areg", "immediate") else 0)
            return 4 + ea(src, False)
        if m == "eor" and dst.mode == "dreg":
            return 8 if long else 4
        return (12 if long else 8) + ea(dst, long)
    if m in ("addi", "subi", "andi", "ori", "eori", "cmpi"):
        dst = ops[1]
        if dst.mode in ("sr", "ccr"):
            return 20
        if dst.mode == "dreg":
            if m == "cmpi":
                return 14 if long else 8
            if m == "andi":
                return 14 if long else 8
            return 16 if long else 8
        if m == "cmpi":
            return (12 if long else 8) + ea(dst, long)
        return (20 if long else 12) + ea(dst, long)
    if m in ("addq", "subq"):
        dst = ops[1]
        if dst.mode == "dreg":
            return 8 if long else 4
        if dst.mode == "areg":
            return 8
        return (12 if long else 8) + ea(dst, long)
    if m in ("addx", "subx"):
        if ops[0].mode == "dreg":
            return 8 if long else 4
        return 30 if long else 18
    if m == "cmpm":
        return 20 if long else 12
    if m in ("clr", "neg", "negx", "not"):
        if ops[0].mode == "dreg":
            return 6 if long else 4
        return (12 if long else 8) + ea(ops[0], long)
    if m == "tst":
        return 4 + ea(ops[0], long)
    if m == "nbcd":
        return 6 if ops[0].mode == "dreg" else 8 + ea(ops[0], False)
    if m in ("abcd", "sbcd"):
        return 6 if ops[0].mode == "dreg" else 18
    if m == "movep":
        return 24 if long else 16
    if m in ("swap", "ext"):
        return 4
    if m == "exg":
        return 6
    if m in ("mulu", "muls", "divu", "divs"):
        return ea(ops[0], False)          # execution time added by the helper
    if m in ("asl", "asr", "lsl", "lsr", "rol", "ror", "roxl", "roxr"):
        if len(ops) == 1:
            return 8 + ea(ops[0], False)
        base = 8 if long else 6
        if ops[0].mode == "quick":
            return base + 2 * ((ops[0].value & 0xFFFFFFFF) or 8)
        return base                        # + 2 * count at run time
    if m in ("btst", "bchg", "bclr", "bset"):
        static = ops[0].mode in ("immediate", "quick")
        dst = ops[1]
        if dst.mode == "dreg":
            if m == "btst":
                return 10 if static else 6
            if m == "bclr":
                return 14 if static else 10
            return 12 if static else 8
        if m == "btst":
            return (8 if static else 4) + ea(dst, False)
        return (12 if static else 8) + ea(dst, False)
    if m == "movem":
        if ops[0].mode == "reglist":
            n = len(ops[0].registers)
            return MOVEM_TO_MEM.get(mode(ops[1]), 8) + n * (8 if long else 4)
        n = len(ops[1].registers)
        return MOVEM_TO_REG.get(mode(ops[0]), 12) + n * (8 if long else 4)
    if m == "link":
        return 16
    if m == "unlk":
        return 12
    if m == "bra":
        return 10
    if m == "bsr":
        return 18
    if m == "bcc":
        return 0                           # 10 taken, 8 (.s) / 12 (.w) not taken: emitted inline
    if m == "dbcc":
        return 0                           # charged by m68k_dbcc
    if m == "jmp":
        return JMP.get(mode(ops[0]), 8)
    if m == "jsr":
        return JSR.get(mode(ops[0]), 16)
    if m == "rts":
        return 16
    if m in ("rte", "rtr"):
        return 20
    if m == "trap":
        return 0                           # exception processing charged by the runtime
    if m == "stop":
        return 4
    if m in ("scc", "tas", "chk"):
        return 4 + ea(ops[-1], False)
    return 4
