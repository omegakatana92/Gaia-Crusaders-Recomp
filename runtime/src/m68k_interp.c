/* 68000 fallback interpreter.  Each case mirrors what tools/recompile68k.py
 * emits for the same instruction (_emit_instruction / emit_instruction):
 * same semantic helpers, same effective-address descriptors, same cycle
 * charges.  The opcode table comes from the translator's disassembler
 * (tools/make_m68k_interp.py); extension words are read here. */
#include "m68k/interp.h"
#include "board/bus.h"

typedef struct reader { uint32_t at; } reader;

static inline uint16_t rd16(reader *r) { uint16_t v = m68k_bus_read16(r->at & 0xFFFFFF); r->at += 2; return v; }
static inline uint32_t rd32(reader *r) { uint32_t hi = rd16(r); return hi << 16 | rd16(r); }

/* immediates as the disassembler presents them (sign-extended or not) */
static uint32_t immediate(reader *r, const m68k_operand_form *f)
{
    switch (f->kind) {
    case M68K_O_IMM_B: { uint32_t v = rd16(r) & 0xFF; return f->reg ? (uint32_t)(int32_t)(int8_t)v : v; }
    case M68K_O_IMM_W: { uint32_t v = rd16(r); return f->reg ? (uint32_t)(int32_t)(int16_t)v : v; }
    case M68K_O_IMM_L: return rd32(r);
    case M68K_O_QUICK: return f->value;
    default: return 0;
    }
}

static m68k_ea ea(reader *r, const m68k_operand_form *f)
{
    switch (f->kind) {
    case M68K_O_DREG: return EA_DREG(f->reg);
    case M68K_O_AREG: return EA_AREG(f->reg);
    case M68K_O_IND: return EA_IND(f->reg);
    case M68K_O_POST: return EA_POST(f->reg);
    case M68K_O_PRE: return EA_PRE(f->reg);
    case M68K_O_DISP: return EA_DISP(f->reg, (int16_t)rd16(r));
    case M68K_O_INDEX: {
        uint16_t x = rd16(r);
        int idx = ((x >> 12) & 7) + ((x & 0x8000) ? 8 : 0);
        return EA_INDEX(f->reg, idx, (x & 0x800) ? 1 : 0, (int8_t)(x & 0xFF));
    }
    case M68K_O_ABS_W: return EA_ABS((uint32_t)(int32_t)(int16_t)rd16(r) & 0xFFFFFF);
    case M68K_O_ABS_L: return EA_ABS(rd32(r) & 0xFFFFFF);
    case M68K_O_PC_DISP: { uint32_t base = r->at; return EA_ABS((base + (uint32_t)(int32_t)(int16_t)rd16(r)) & 0xFFFFFF); }
    case M68K_O_PC_INDEX: {
        uint32_t base = r->at;
        uint16_t x = rd16(r);
        int idx = ((x >> 12) & 7) + ((x & 0x8000) ? 8 : 0);
        return EA_ABS_INDEX((base + (uint32_t)(int32_t)(int8_t)(x & 0xFF)) & 0xFFFFFF, idx, (x & 0x800) ? 1 : 0);
    }
    case M68K_O_IMM_B: case M68K_O_IMM_W: case M68K_O_IMM_L: case M68K_O_QUICK:
        return EA_IMM(immediate(r, f));
    default: return EA_IMM(0);
    }
}

static inline int is_imm(const m68k_operand_form *f)
{
    return f->kind == M68K_O_IMM_B || f->kind == M68K_O_IMM_W || f->kind == M68K_O_IMM_L || f->kind == M68K_O_QUICK;
}

static uint16_t rev16(uint16_t v)
{
    uint16_t o = 0;
    for (int i = 0; i < 16; i++) if (v & (1u << i)) o |= (uint16_t)(1u << (15 - i));
    return o;
}

static int popcount16(uint16_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

uint32_t m68k_interp_step(m68k_state *s, uint32_t pc)
{
    uint16_t op = m68k_bus_read16(pc & 0xFFFFFF);
    const m68k_decoded *d = &m68k_decode_table[op];
    reader r = { pc + 2 };
    const m68k_operand_form *A = &d->a, *B = &d->b;
    int size = d->size;
    if (d->cls == M68K_I_INVALID) return m68k_unsupported(s, pc & 0xFFFFFF);
    if (d->cycles && d->cls != M68K_I_SHIFT_REG && d->cls != M68K_I_BCC) s->cycles += d->cycles;
#define NEXT (r.at & 0xFFFFFF)
    switch (d->cls) {
    case M68K_I_NOP: case M68K_I_RESET: return NEXT;
    case M68K_I_MOVE_USP_TO: m68k_move_to_usp(s, s->a[A->reg & 7]); return NEXT;
    case M68K_I_MOVE_USP_FROM: s->a[B->reg & 7] = m68k_move_from_usp(s); return NEXT;
    case M68K_I_MOVE: { m68k_ea src = ea(&r, A); m68k_ea dst = ea(&r, B); m68k_move(s, src, dst, size); return NEXT; }
    case M68K_I_MOVE_TO_SR:
        if (is_imm(A)) m68k_set_sr(s, immediate(&r, A) & 0xFFFF);
        else { m68k_ea src = ea(&r, A); m68k_move_to_sr(s, src, size); }
        return NEXT;
    case M68K_I_MOVE_TO_CCR:
        if (is_imm(A)) m68k_set_ccr(s, immediate(&r, A) & 0xFF);
        else { m68k_ea src = ea(&r, A); m68k_move_to_ccr(s, src, size); }
        return NEXT;
    case M68K_I_MOVE_FROM_SR: { m68k_ea dst = ea(&r, B); m68k_move_from_sr(s, dst, size); return NEXT; }
    case M68K_I_MOVE_FROM_CCR: { m68k_ea dst = ea(&r, B); m68k_move_from_ccr(s, dst, size); return NEXT; }
    case M68K_I_MOVEA: { m68k_ea src = ea(&r, A); m68k_movea(s, src, B->reg & 7, size); return NEXT; }
    case M68K_I_MOVEQ: m68k_moveq(s, (int32_t)A->value, B->reg); return NEXT;
    case M68K_I_LEA: { m68k_ea src = ea(&r, A); m68k_lea(s, src, B->reg & 7); return NEXT; }
    case M68K_I_PEA: { m68k_ea src = ea(&r, A); m68k_pea(s, src); return NEXT; }
    case M68K_I_ADD: case M68K_I_SUB: {
        m68k_ea src = ea(&r, A), dst = ea(&r, B);
        if (d->cls == M68K_I_ADD) m68k_add(s, src, dst, size, 0); else m68k_sub(s, src, dst, size, 0);
        return NEXT;
    }
    case M68K_I_ADDA: case M68K_I_SUBA: {
        m68k_ea src = ea(&r, A);
        if (d->cls == M68K_I_ADDA) m68k_adda(s, src, B->reg & 7, size); else m68k_suba(s, src, B->reg & 7, size);
        return NEXT;
    }
    case M68K_I_ADDI: case M68K_I_SUBI: case M68K_I_ANDI: case M68K_I_ORI: case M68K_I_EORI: case M68K_I_CMPI: {
        uint32_t v = immediate(&r, A);
        m68k_ea dst = ea(&r, B);
        switch (d->cls) {
        case M68K_I_ADDI: m68k_addi(s, v, dst, size); break;
        case M68K_I_SUBI: m68k_subi(s, v, dst, size); break;
        case M68K_I_ANDI: m68k_andi(s, v, dst, size); break;
        case M68K_I_ORI: m68k_ori(s, v, dst, size); break;
        case M68K_I_EORI: m68k_eori(s, v, dst, size); break;
        default: m68k_cmpi(s, v, dst, size); break;
        }
        return NEXT;
    }
    case M68K_I_ANDI_CCR: m68k_andi_ccr(s, immediate(&r, A)); return NEXT;
    case M68K_I_ORI_CCR: m68k_ori_ccr(s, immediate(&r, A)); return NEXT;
    case M68K_I_EORI_CCR: m68k_eori_ccr(s, immediate(&r, A)); return NEXT;
    case M68K_I_ANDI_SR: { uint32_t v = immediate(&r, A); m68k_set_sr(s, s->sr & v); return NEXT; }
    case M68K_I_ORI_SR: { uint32_t v = immediate(&r, A); m68k_set_sr(s, s->sr | v); return NEXT; }
    case M68K_I_EORI_SR: { uint32_t v = immediate(&r, A); m68k_set_sr(s, s->sr ^ v); return NEXT; }
    case M68K_I_ADDQ: { m68k_ea dst = ea(&r, B); m68k_addq(s, A->value, dst, size); return NEXT; }
    case M68K_I_SUBQ: { m68k_ea dst = ea(&r, B); m68k_subq(s, A->value, dst, size); return NEXT; }
    case M68K_I_ADDX: case M68K_I_SUBX: {
        m68k_ea src = ea(&r, A);
        int dreg = B->reg + (B->kind == M68K_O_PRE ? 8 : 0);   /* reg_number(): An -> 8 + n */
        if (d->cls == M68K_I_ADDX) m68k_addx(s, src, dreg, size); else m68k_subx(s, src, dreg, size);
        return NEXT;
    }
    case M68K_I_CMP: case M68K_I_CMPM: { m68k_ea a = ea(&r, A), b = ea(&r, B); m68k_cmp(s, a, b, size); return NEXT; }
    case M68K_I_CMPA: { m68k_ea src = ea(&r, A); m68k_cmpa(s, src, B->reg & 7, size); return NEXT; }
    case M68K_I_AND: case M68K_I_OR: case M68K_I_EOR: {
        m68k_ea a = ea(&r, A), b = ea(&r, B);
        if (d->cls == M68K_I_AND) m68k_and(s, a, b, size);
        else if (d->cls == M68K_I_OR) m68k_or(s, a, b, size);
        else m68k_eor(s, a, b, size);
        return NEXT;
    }
    case M68K_I_NEGX: { m68k_ea a = ea(&r, A); m68k_negx(s, a, size); return NEXT; }
    case M68K_I_NBCD: { m68k_ea a = ea(&r, A); m68k_nbcd(s, a); return NEXT; }
    case M68K_I_ABCD: case M68K_I_SBCD: {
        m68k_ea a = ea(&r, A), b = ea(&r, B);
        if (d->cls == M68K_I_ABCD) m68k_abcd(s, a, b); else m68k_sbcd(s, a, b);
        return NEXT;
    }
    case M68K_I_MOVEP_TO_MEM: { int16_t disp = (int16_t)rd16(&r); m68k_movep(s, B->reg & 7, disp, A->reg, size, 1); return NEXT; }
    case M68K_I_MOVEP_TO_REG: { int16_t disp = (int16_t)rd16(&r); m68k_movep(s, A->reg & 7, disp, B->reg, size, 0); return NEXT; }
    case M68K_I_CLR: { m68k_ea a = ea(&r, A); m68k_clr(s, a, size); return NEXT; }
    case M68K_I_TST: { m68k_ea a = ea(&r, A); m68k_tst(s, a, size); return NEXT; }
    case M68K_I_NEG: { m68k_ea a = ea(&r, A); m68k_neg(s, a, size); return NEXT; }
    case M68K_I_NOT: { m68k_ea a = ea(&r, A); m68k_not(s, a, size); return NEXT; }
    case M68K_I_SWAP: m68k_swap(s, A->reg); return NEXT;
    case M68K_I_EXT: m68k_ext(s, A->reg, size); return NEXT;
    case M68K_I_MULU: case M68K_I_MULS: case M68K_I_DIVU: case M68K_I_DIVS: {
        m68k_ea src = ea(&r, A);
        switch (d->cls) {
        case M68K_I_MULU: m68k_mulu(s, src, B->reg); break;
        case M68K_I_MULS: m68k_muls(s, src, B->reg); break;
        case M68K_I_DIVU: m68k_divu(s, src, B->reg); break;
        default: m68k_divs(s, src, B->reg); break;
        }
        return NEXT;
    }
    case M68K_I_SHIFT_MEM: { m68k_ea a = ea(&r, A); m68k_shift(s, d->extra, 1u, a, 2); return NEXT; }
    case M68K_I_SHIFT_IMM: { m68k_ea b = ea(&r, B); m68k_shift(s, d->extra, A->value, b, size); return NEXT; }
    case M68K_I_SHIFT_REG: {
        uint32_t count = s->d[A->reg] & 63u;
        s->cycles += d->cycles + 2u * count;
        m68k_ea b = ea(&r, B);
        m68k_shift(s, d->extra, count, b, size);
        return NEXT;
    }
    case M68K_I_BIT_IMM: { uint32_t bit = immediate(&r, A); m68k_ea b = ea(&r, B); m68k_bitop(s, d->extra, bit, b); return NEXT; }
    case M68K_I_BIT_REG: { m68k_ea b = ea(&r, B); m68k_bitop(s, d->extra, s->d[A->reg], b); return NEXT; }
    case M68K_I_MOVEM_TO_MEM: {
        uint16_t mask = rd16(&r);
        if (B->kind == M68K_O_PRE) mask = rev16(mask);
        s->cycles += (uint64_t)popcount16(mask) * (size == 4 ? 8u : 4u);
        m68k_ea dst = ea(&r, B);
        m68k_movem_to_mem(s, mask, dst, size);
        return NEXT;
    }
    case M68K_I_MOVEM_TO_REGS: {
        uint16_t mask = rd16(&r);
        s->cycles += (uint64_t)popcount16(mask) * (size == 4 ? 8u : 4u);
        m68k_ea src = ea(&r, A);
        m68k_movem_to_regs(s, mask, src, size);
        return NEXT;
    }
    case M68K_I_LINK: { uint32_t v = immediate(&r, B); m68k_link(s, A->reg & 7, (int32_t)v); return NEXT; }
    case M68K_I_UNLK: m68k_unlk(s, A->reg & 7); return NEXT;
    case M68K_I_EXG: {
        int r1 = A->reg + (A->kind == M68K_O_AREG ? 8 : 0), r2 = B->reg + (B->kind == M68K_O_AREG ? 8 : 0);
        m68k_exg(s, r1, r2, A->kind == M68K_O_DREG && B->kind == M68K_O_DREG);
        return NEXT;
    }
    case M68K_I_BRA: case M68K_I_BSR: case M68K_I_BCC: {
        int32_t disp = d->short_branch ? (int8_t)(op & 0xFF) : (int16_t)rd16(&r);
        uint32_t target = (pc + 2 + (uint32_t)disp) & 0xFFFFFF;
        if (d->cls == M68K_I_BRA) return target;
        if (d->cls == M68K_I_BSR) { m68k_push32(s, NEXT); return target; }
        if (d->cond == 0) { s->cycles += 10; return target; }
        if (d->cond == 1) { s->cycles += d->short_branch ? 8u : 12u; return NEXT; }
        if (m68k_cond(s, d->cond)) { s->cycles += 10; return target; }
        s->cycles += d->short_branch ? 8u : 12u;
        return NEXT;
    }
    case M68K_I_DBCC: {
        uint32_t target = (pc + 2 + (uint32_t)(int32_t)(int16_t)rd16(&r)) & 0xFFFFFF;
        return m68k_dbcc(s, A->reg, d->cond, target) ? target : NEXT;
    }
    case M68K_I_JMP: { m68k_ea a = ea(&r, A); return m68k_ea_address(s, a); }
    case M68K_I_JSR: { m68k_ea a = ea(&r, A); m68k_push32(s, NEXT); return m68k_ea_address(s, a); }
    case M68K_I_RTS: return m68k_pop32(s);
    case M68K_I_RTE: m68k_rte(s); return s->pc;
    case M68K_I_RTR: m68k_rtr(s); return s->pc;
    case M68K_I_TRAP: m68k_trap(s, A->value & 15); return s->pc;
    case M68K_I_STOP: { uint32_t v = immediate(&r, A); m68k_stop(s, v & 0xFFFF); return NEXT; }
    default: return m68k_unsupported(s, pc & 0xFFFFFF);
    }
#undef NEXT
}
