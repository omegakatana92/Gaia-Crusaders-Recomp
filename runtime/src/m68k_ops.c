/* MC68000 semantics for the statically translated program.
 *
 * The build-time translator emits one call per instruction; there is no
 * instruction fetch or decode at run time.  Flag behaviour follows the
 * MC68000 User's Manual; exception frames and byte/word register writes follow
 * MAME's Musashi core (src/devices/cpu/m68000).
 */
#include "m68k/m68k.h"
#include "board/bus.h"

#include <stdio.h>
#include <string.h>

static inline uint32_t size_mask(int size)
{
    return size == 1 ? 0xFFu : size == 2 ? 0xFFFFu : 0xFFFFFFFFu;
}

static inline unsigned size_bits(int size)
{
    return (unsigned)(size * 8);
}

static inline uint32_t sign_extend(uint32_t value, int size)
{
    if (size == 1) return (uint32_t)(int32_t)(int8_t)value;
    if (size == 2) return (uint32_t)(int32_t)(int16_t)value;
    return value;
}

static inline uint32_t read_mem(uint32_t address, int size)
{
    if (size == 1) return m68k_bus_read8(address);
    if (size == 2) return m68k_bus_read16(address);
    return m68k_bus_read32(address);
}

static inline void write_mem(uint32_t address, int size, uint32_t value)
{
    if (size == 1) m68k_bus_write8(address, (uint8_t)value);
    else if (size == 2) m68k_bus_write16(address, (uint16_t)value);
    else m68k_bus_write32(address, value);
}

static inline uint32_t index_value(const m68k_state *s, uint8_t encoded)
{
    unsigned reg = encoded & 0x0F;
    uint32_t value = reg < 8 ? s->d[reg] : s->a[reg - 8];
    if (!(encoded & 0x10))
        value = (uint32_t)(int32_t)(int16_t)value;
    return value;
}

/* (A7)+ / -(A7) move by 2 for byte operations to keep the stack aligned. */
static inline uint32_t ea_step(m68k_ea ea, int size)
{
    return (size == 1 && ea.reg == 7) ? 2u : (uint32_t)size;
}

/* Read-modify-write destinations: evaluate the effective address exactly
 * once (one post-increment / pre-decrement), as the 68000 does, and return
 * an absolute EA for the subsequent read and write. */
static m68k_ea resolve_rmw(m68k_state *s, m68k_ea ea, int size)
{
    uint32_t address;
    switch (ea.mode) {
    case M68K_EA_DREG:
    case M68K_EA_AREG:
    case M68K_EA_IMM:
        return ea;
    case M68K_EA_POSTINC:
        address = s->a[ea.reg];
        s->a[ea.reg] += ea_step(ea, size);
        return EA_ABS(address);
    case M68K_EA_PREDEC:
        s->a[ea.reg] -= ea_step(ea, size);
        return EA_ABS(s->a[ea.reg]);
    default:
        return EA_ABS(m68k_ea_address(s, ea));
    }
}

uint32_t m68k_ea_address(m68k_state *s, m68k_ea ea)
{
    switch (ea.mode) {
    case M68K_EA_IND:
    case M68K_EA_POSTINC:
    case M68K_EA_PREDEC:
        return s->a[ea.reg];
    case M68K_EA_DISP:
        return s->a[ea.reg] + (uint32_t)ea.disp;
    case M68K_EA_INDEX:
        return s->a[ea.reg] + (uint32_t)ea.disp + index_value(s, ea.index);
    case M68K_EA_ABS:
    case M68K_EA_ABS_INDEX:
        return ea.value + (ea.mode == M68K_EA_ABS_INDEX ? index_value(s, ea.index) : 0u);
    case M68K_EA_DREG:
        return s->d[ea.reg];
    case M68K_EA_AREG:
        return s->a[ea.reg];
    case M68K_EA_IMM:
        return ea.value;
    default:
        return 0;
    }
}

uint32_t m68k_ea_address_indexed(m68k_state *s, m68k_ea ea)
{
    return m68k_ea_address(s, ea);
}

uint32_t m68k_get(m68k_state *s, m68k_ea ea, int size)
{
    uint32_t address;
    uint32_t value;
    switch (ea.mode) {
    case M68K_EA_DREG:
        return s->d[ea.reg] & size_mask(size);
    case M68K_EA_AREG:
        return s->a[ea.reg] & size_mask(size);
    case M68K_EA_IMM:
        return ea.value & size_mask(size);
    case M68K_EA_POSTINC:
        address = s->a[ea.reg];
        s->a[ea.reg] += ea_step(ea, size);
        return read_mem(address, size);
    case M68K_EA_PREDEC:
        s->a[ea.reg] -= ea_step(ea, size);
        return read_mem(s->a[ea.reg], size);
    default:
        address = m68k_ea_address(s, ea);
        value = read_mem(address, size);
        return value;
    }
}

void m68k_put(m68k_state *s, m68k_ea ea, int size, uint32_t value)
{
    uint32_t address;
    switch (ea.mode) {
    case M68K_EA_DREG:
        if (size == 1) s->d[ea.reg] = (s->d[ea.reg] & 0xFFFFFF00u) | (value & 0xFFu);
        else if (size == 2) s->d[ea.reg] = (s->d[ea.reg] & 0xFFFF0000u) | (value & 0xFFFFu);
        else s->d[ea.reg] = value;
        return;
    case M68K_EA_AREG:
        s->a[ea.reg] = value;
        return;
    case M68K_EA_POSTINC:
        address = s->a[ea.reg];
        write_mem(address, size, value);
        s->a[ea.reg] += ea_step(ea, size);
        return;
    case M68K_EA_PREDEC:
        s->a[ea.reg] -= ea_step(ea, size);
        write_mem(s->a[ea.reg], size, value);
        return;
    default:
        address = m68k_ea_address(s, ea);
        write_mem(address, size, value);
        return;
    }
}

void m68k_push16(m68k_state *s, uint16_t value)
{
    s->a[7] -= 2;
    m68k_bus_write16(s->a[7], value);
}

uint16_t m68k_pop16(m68k_state *s)
{
    uint16_t value = m68k_bus_read16(s->a[7]);
    s->a[7] += 2;
    return value;
}

void m68k_push32(m68k_state *s, uint32_t value)
{
    s->a[7] -= 4;
    m68k_bus_write32(s->a[7], value);
}

uint32_t m68k_pop32(m68k_state *s)
{
    uint32_t value = m68k_bus_read32(s->a[7]);
    s->a[7] += 4;
    return value;
}

static inline void set_nz(m68k_state *s, uint32_t value, int size)
{
    uint32_t mask = size_mask(size);
    unsigned bits = size_bits(size);
    uint32_t v = value & mask;
    s->sr = (uint16_t)((s->sr & ~(M68K_N | M68K_Z)) |
                       (v == 0 ? M68K_Z : 0) |
                       ((v >> (bits - 1)) & 1 ? M68K_N : 0));
}

static inline void set_logic_flags(m68k_state *s, uint32_t value, int size)
{
    set_nz(s, value, size);
    s->sr = (uint16_t)(s->sr & ~(M68K_V | M68K_C));
}

/* Every SR write goes through here so a change of the S bit swaps the
 * active stack pointer (A7) with the saved one, as on the real 68000. */
void m68k_set_sr(m68k_state *s, uint32_t value)
{
    uint16_t old = s->sr;
    s->sr = (uint16_t)(value & 0xA71Fu);
    if ((old ^ s->sr) & M68K_S) {
        uint32_t t = s->a[7];
        s->a[7] = s->other_sp;
        s->other_sp = t;
    }
}

void m68k_move_to_usp(m68k_state *s, uint32_t value)
{
    if (s->sr & M68K_S) s->other_sp = value;
    else s->a[7] = value;
}

uint32_t m68k_move_from_usp(const m68k_state *s)
{
    return (s->sr & M68K_S) ? s->other_sp : s->a[7];
}

void m68k_set_ccr(m68k_state *s, uint32_t value)
{
    s->sr = (uint16_t)((s->sr & 0xFF00u) | (value & 0x1Fu));
}

void m68k_move_from_sr(m68k_state *s, m68k_ea dst, int size)
{
    m68k_put(s, dst, size, size == 1 ? (s->sr & 0xFFu) : s->sr);
}

void m68k_move_from_ccr(m68k_state *s, m68k_ea dst, int size)
{
    m68k_put(s, dst, size, s->sr & 0x1Fu);
}

void m68k_ori_ccr(m68k_state *s, uint32_t value)
{
    s->sr = (uint16_t)(s->sr | (value & 0x1Fu));
}

void m68k_andi_ccr(m68k_state *s, uint32_t value)
{
    s->sr = (uint16_t)(s->sr & (0xFF00u | (value & 0x1Fu)));
}

void m68k_eori_ccr(m68k_state *s, uint32_t value)
{
    s->sr = (uint16_t)(s->sr ^ (value & 0x1Fu));
}

int m68k_cond(const m68k_state *s, int condition)
{
    int c = (s->sr & M68K_C) != 0;
    int v = (s->sr & M68K_V) != 0;
    int z = (s->sr & M68K_Z) != 0;
    int n = (s->sr & M68K_N) != 0;
    switch (condition & 0x0F) {
    case 0x0: return 1;                 /* t  */
    case 0x1: return 0;                 /* f  */
    case 0x2: return !c && !z;          /* hi */
    case 0x3: return c || z;            /* ls */
    case 0x4: return !c;                /* cc */
    case 0x5: return c;                 /* cs */
    case 0x6: return !z;                /* ne */
    case 0x7: return z;                 /* eq */
    case 0x8: return !v;                /* vc */
    case 0x9: return v;                 /* vs */
    case 0xA: return !n;                /* pl */
    case 0xB: return n;                 /* mi */
    case 0xC: return n == v;            /* ge */
    case 0xD: return n != v;            /* lt */
    case 0xE: return !z && n == v;      /* gt */
    case 0xF: return z || n != v;       /* le */
    }
    return 0;
}

int m68k_dbcc(m68k_state *s, int reg, int condition, uint32_t target)
{
    (void)target;
    if (m68k_cond(s, condition)) { s->cycles += 12; return 0; }
    uint32_t counter = ((s->d[reg] & 0xFFFFu) - 1u) & 0xFFFFu;
    s->d[reg] = (s->d[reg] & 0xFFFF0000u) | counter;
    s->cycles += counter != 0xFFFFu ? 10 : 14;
    return counter != 0xFFFFu;
}

void m68k_move(m68k_state *s, m68k_ea src, m68k_ea dst, int size)
{
    uint32_t value = m68k_get(s, src, size);
    m68k_put(s, dst, size, value);
    set_logic_flags(s, value, size);
}

void m68k_movea(m68k_state *s, m68k_ea src, int areg, int size)
{
    uint32_t value = m68k_get(s, src, size);
    if (size == 2) value = sign_extend(value, 2);
    s->a[areg] = value;
}

void m68k_moveq(m68k_state *s, int32_t value, int dreg)
{
    s->d[dreg] = (uint32_t)value;
    set_logic_flags(s, s->d[dreg], 4);
}

void m68k_move_to_sr(m68k_state *s, m68k_ea src, int size)
{
    m68k_set_sr(s, m68k_get(s, src, size));
}

void m68k_move_to_ccr(m68k_state *s, m68k_ea src, int size)
{
    m68k_set_ccr(s, m68k_get(s, src, size));
}

static void add_flags(m68k_state *s, uint32_t a, uint32_t b, uint32_t extra,
                      uint32_t result, int size)
{
    uint32_t mask = size_mask(size);
    unsigned bits = size_bits(size);
    uint32_t sign = 1u << (bits - 1);
    uint64_t full = (uint64_t)(a & mask) + (uint64_t)(b & mask) + extra;
    uint32_t r = result & mask;
    int carry = full > (uint64_t)mask;
    int overflow = ((~(a ^ b) & (a ^ r)) & sign) != 0;
    s->sr = (uint16_t)((s->sr & ~(M68K_C | M68K_V | M68K_X | M68K_N | M68K_Z)) |
                       (carry ? M68K_C : 0) | (carry ? M68K_X : 0) |
                       (overflow ? M68K_V : 0) |
                       (r == 0 ? M68K_Z : 0) |
                       (((r >> (bits - 1)) & 1u) ? M68K_N : 0));
}

static void sub_flags(m68k_state *s, uint32_t a, uint32_t b, uint32_t extra,
                      uint32_t result, int size)
{
    uint32_t mask = size_mask(size);
    unsigned bits = size_bits(size);
    uint32_t sign = 1u << (bits - 1);
    uint32_t am = a & mask, bm = b & mask, r = result & mask;
    uint64_t subtrahend = (uint64_t)bm + extra;
    int borrow = (uint64_t)am < subtrahend;
    int overflow = (((a ^ b) & (a ^ r)) & sign) != 0;
    s->sr = (uint16_t)((s->sr & ~(M68K_C | M68K_V | M68K_X | M68K_N | M68K_Z)) |
                       (borrow ? M68K_C : 0) | (borrow ? M68K_X : 0) |
                       (overflow ? M68K_V : 0) |
                       (r == 0 ? M68K_Z : 0) |
                       (((r >> (bits - 1)) & 1u) ? M68K_N : 0));
}

void m68k_add(m68k_state *s, m68k_ea src, m68k_ea dst, int size, int with_x)
{
    uint32_t b = m68k_get(s, src, size);
    dst = resolve_rmw(s, dst, size);
    uint32_t a = m68k_get(s, dst, size);
    uint32_t extra = (with_x && (s->sr & M68K_X)) ? 1u : 0u;
    uint32_t result = (a + b + extra) & size_mask(size);
    m68k_put(s, dst, size, result);
    add_flags(s, a, b, extra, result, size);
}

void m68k_sub(m68k_state *s, m68k_ea src, m68k_ea dst, int size, int with_x)
{
    uint32_t b = m68k_get(s, src, size);
    dst = resolve_rmw(s, dst, size);
    uint32_t a = m68k_get(s, dst, size);
    uint32_t extra = (with_x && (s->sr & M68K_X)) ? 1u : 0u;
    uint32_t result = (a - b - extra) & size_mask(size);
    m68k_put(s, dst, size, result);
    sub_flags(s, a, b, extra, result, size);
}

void m68k_adda(m68k_state *s, m68k_ea src, int areg, int size)
{
    uint32_t value = m68k_get(s, src, size);
    if (size == 2) value = sign_extend(value, 2);
    s->a[areg] += value;
}

void m68k_suba(m68k_state *s, m68k_ea src, int areg, int size)
{
    uint32_t value = m68k_get(s, src, size);
    if (size == 2) value = sign_extend(value, 2);
    s->a[areg] -= value;
}

void m68k_addi(m68k_state *s, uint32_t value, m68k_ea dst, int size)
{
    m68k_add(s, EA_IMM(value), dst, size, 0);
}

void m68k_subi(m68k_state *s, uint32_t value, m68k_ea dst, int size)
{
    m68k_sub(s, EA_IMM(value), dst, size, 0);
}

void m68k_addq(m68k_state *s, uint32_t value, m68k_ea dst, int size)
{
    if (dst.mode == M68K_EA_AREG) {
        s->a[dst.reg] += value;
        return;
    }
    m68k_add(s, EA_IMM(value), dst, size, 0);
}

void m68k_subq(m68k_state *s, uint32_t value, m68k_ea dst, int size)
{
    if (dst.mode == M68K_EA_AREG) {
        s->a[dst.reg] -= value;
        return;
    }
    m68k_sub(s, EA_IMM(value), dst, size, 0);
}

void m68k_addx(m68k_state *s, m68k_ea src, int dreg, int size)
{
    uint32_t b = m68k_get(s, src, size);
    uint32_t a = s->d[dreg] & size_mask(size);
    uint32_t extra = (s->sr & M68K_X) ? 1u : 0u;
    uint32_t result = (a + b + extra) & size_mask(size);
    int old_z = (s->sr & M68K_Z) != 0;
    m68k_put(s, EA_DREG(dreg), size, result);
    add_flags(s, a, b, extra, result, size);
    if (result != 0) s->sr = (uint16_t)(s->sr & ~M68K_Z);
    else if (old_z) s->sr = (uint16_t)(s->sr | M68K_Z);
    else s->sr = (uint16_t)(s->sr & ~M68K_Z);
}

void m68k_subx(m68k_state *s, m68k_ea src, int dreg, int size)
{
    uint32_t b = m68k_get(s, src, size);
    uint32_t a = s->d[dreg] & size_mask(size);
    uint32_t extra = (s->sr & M68K_X) ? 1u : 0u;
    uint32_t result = (a - b - extra) & size_mask(size);
    int old_z = (s->sr & M68K_Z) != 0;
    m68k_put(s, EA_DREG(dreg), size, result);
    sub_flags(s, a, b, extra, result, size);
    if (result != 0) s->sr = (uint16_t)(s->sr & ~M68K_Z);
    else if (old_z) s->sr = (uint16_t)(s->sr | M68K_Z);
    else s->sr = (uint16_t)(s->sr & ~M68K_Z);
}

static void compare_flags(m68k_state *s, uint32_t a, uint32_t b, int size)
{
    uint32_t mask = size_mask(size);
    unsigned bits = size_bits(size);
    uint32_t result = (a - b) & mask;
    uint32_t sign = 1u << (bits - 1);
    int borrow = (a & mask) < (b & mask);
    int overflow = (((a ^ b) & (a ^ result)) & sign) != 0;
    s->sr = (uint16_t)((s->sr & ~(M68K_C | M68K_V | M68K_N | M68K_Z)) |
                       (borrow ? M68K_C : 0) | (overflow ? M68K_V : 0) |
                       (result == 0 ? M68K_Z : 0) |
                       (((result >> (bits - 1)) & 1u) ? M68K_N : 0));
}

void m68k_cmp(m68k_state *s, m68k_ea src, m68k_ea dst, int size)
{
    uint32_t b = m68k_get(s, src, size);
    uint32_t a = m68k_get(s, dst, size);
    compare_flags(s, a, b, size);
}

void m68k_cmpa(m68k_state *s, m68k_ea src, int areg, int size)
{
    uint32_t b = m68k_get(s, src, size);
    if (size == 2) b = sign_extend(b, 2);
    compare_flags(s, s->a[areg], b, 4);
}

void m68k_cmpi(m68k_state *s, uint32_t value, m68k_ea dst, int size)
{
    uint32_t a = m68k_get(s, dst, size);
    compare_flags(s, a, value & size_mask(size), size);
}

void m68k_and(m68k_state *s, m68k_ea src, m68k_ea dst, int size)
{
    uint32_t b = m68k_get(s, src, size);
    dst = resolve_rmw(s, dst, size);
    uint32_t result = m68k_get(s, dst, size) & b;
    m68k_put(s, dst, size, result);
    set_logic_flags(s, result, size);
}

void m68k_or(m68k_state *s, m68k_ea src, m68k_ea dst, int size)
{
    uint32_t b = m68k_get(s, src, size);
    dst = resolve_rmw(s, dst, size);
    uint32_t result = m68k_get(s, dst, size) | b;
    m68k_put(s, dst, size, result);
    set_logic_flags(s, result, size);
}

void m68k_eor(m68k_state *s, m68k_ea src, m68k_ea dst, int size)
{
    uint32_t b = m68k_get(s, src, size);
    dst = resolve_rmw(s, dst, size);
    uint32_t result = m68k_get(s, dst, size) ^ b;
    m68k_put(s, dst, size, result);
    set_logic_flags(s, result, size);
}

void m68k_andi(m68k_state *s, uint32_t value, m68k_ea dst, int size)
{
    dst = resolve_rmw(s, dst, size);
    uint32_t result = m68k_get(s, dst, size) & value;
    m68k_put(s, dst, size, result);
    set_logic_flags(s, result, size);
}

void m68k_ori(m68k_state *s, uint32_t value, m68k_ea dst, int size)
{
    dst = resolve_rmw(s, dst, size);
    uint32_t result = m68k_get(s, dst, size) | value;
    m68k_put(s, dst, size, result);
    set_logic_flags(s, result, size);
}

void m68k_eori(m68k_state *s, uint32_t value, m68k_ea dst, int size)
{
    dst = resolve_rmw(s, dst, size);
    uint32_t result = m68k_get(s, dst, size) ^ value;
    m68k_put(s, dst, size, result);
    set_logic_flags(s, result, size);
}

void m68k_clr(m68k_state *s, m68k_ea dst, int size)
{
    m68k_put(s, dst, size, 0);
    s->sr = (uint16_t)((s->sr & ~(M68K_N | M68K_V | M68K_C)) | M68K_Z);
}

void m68k_tst(m68k_state *s, m68k_ea dst, int size)
{
    uint32_t value = m68k_get(s, dst, size);
    set_logic_flags(s, value, size);
}

void m68k_neg(m68k_state *s, m68k_ea dst, int size)
{
    dst = resolve_rmw(s, dst, size);
    uint32_t value = m68k_get(s, dst, size);
    uint32_t result = (0u - value) & size_mask(size);
    uint32_t sign = 1u << (size_bits(size) - 1);
    m68k_put(s, dst, size, result);
    s->sr = (uint16_t)((s->sr & ~(M68K_C | M68K_V | M68K_X | M68K_N | M68K_Z)) |
                       (value != 0 ? M68K_C : 0) | (value != 0 ? M68K_X : 0) |
                       ((value & size_mask(size)) == sign ? M68K_V : 0) |
                       (result == 0 ? M68K_Z : 0) |
                       (((result & sign) != 0) ? M68K_N : 0));
}

void m68k_not(m68k_state *s, m68k_ea dst, int size)
{
    dst = resolve_rmw(s, dst, size);
    uint32_t result = (~m68k_get(s, dst, size)) & size_mask(size);
    m68k_put(s, dst, size, result);
    set_logic_flags(s, result, size);
}

void m68k_swap(m68k_state *s, int dreg)
{
    uint32_t value = s->d[dreg];
    uint32_t result = (value << 16) | (value >> 16);
    s->d[dreg] = result;
    set_logic_flags(s, result, 4);
}

void m68k_ext(m68k_state *s, int dreg, int size)
{
    /* EXT.W: byte -> word (upper word kept); EXT.L: word -> long */
    uint32_t value = s->d[dreg];
    if (size == 2) {
        uint32_t result = (uint16_t)(int16_t)(int8_t)value;
        s->d[dreg] = (value & 0xFFFF0000u) | result;
        set_logic_flags(s, result, 2);
    } else {
        uint32_t result = (uint32_t)(int32_t)(int16_t)value;
        s->d[dreg] = result;
        set_logic_flags(s, result, 4);
    }
}

/* Division timing after Jorge Cwik, "Exact timing of 68000 DIVU/DIVS". */
static uint32_t divu_cycles(uint32_t dividend, uint16_t divisor)
{
    if (!divisor) return 38;
    if ((dividend >> 16) >= divisor) return 10;
    uint32_t mcycles = 38, hdivisor = (uint32_t)divisor << 16;
    for (int i = 0; i < 15; i++) {
        uint32_t temp = dividend;
        dividend <<= 1;
        if ((int32_t)temp < 0) dividend -= hdivisor;
        else { mcycles += 2; if (dividend >= hdivisor) { dividend -= hdivisor; mcycles--; } }
    }
    return mcycles * 2;
}

static uint32_t divs_cycles(int32_t dividend, int16_t divisor)
{
    if (!divisor) return 38;
    uint32_t mcycles = 6;
    if (dividend < 0) mcycles++;
    uint32_t adend = dividend < 0 ? 0u - (uint32_t)dividend : (uint32_t)dividend;
    uint32_t asor = divisor < 0 ? (uint32_t)(-(int32_t)divisor) : (uint32_t)divisor;
    if ((adend >> 16) >= asor) return (mcycles + 2) * 2;
    uint32_t aquot = adend / asor;
    mcycles = 55;
    if (divisor >= 0) { if (dividend >= 0) mcycles--; else mcycles++; }
    for (int i = 0; i < 15; i++) { if ((int16_t)aquot >= 0) mcycles++; aquot <<= 1; }
    return mcycles * 2;
}

void m68k_mulu(m68k_state *s, m68k_ea src, int dreg)
{
    uint32_t v = m68k_get(s, src, 2) & 0xFFFFu, n = 0;
    for (uint32_t t = v; t; t >>= 1) n += t & 1u;
    s->cycles += 38u + 2u * n;
    uint32_t result = (s->d[dreg] & 0xFFFFu) * v;
    s->d[dreg] = result;
    set_logic_flags(s, result, 4);
}

void m68k_muls(m68k_state *s, m68k_ea src, int dreg)
{
    int32_t a = (int16_t)(s->d[dreg] & 0xFFFFu);
    uint32_t v = m68k_get(s, src, 2) & 0xFFFFu, n = 0;
    for (uint32_t t = ((v << 1) ^ v) & 0xFFFFu; t; t &= t - 1u) n++;
    s->cycles += 38u + 2u * n;
    int32_t b = (int16_t)v;
    uint32_t result = (uint32_t)(a * b);
    s->d[dreg] = result;
    set_logic_flags(s, result, 4);
}

void m68k_divu(m68k_state *s, m68k_ea src, int dreg)
{
    uint32_t divisor = m68k_get(s, src, 2) & 0xFFFFu;
    uint32_t dividend = s->d[dreg];
    s->cycles += divu_cycles(dividend, (uint16_t)divisor);
    if (divisor == 0) {
        s->sr = (uint16_t)((s->sr & ~(M68K_C | M68K_V | M68K_N | M68K_Z)) | M68K_V | M68K_N);
        return;
    }
    uint32_t quotient = dividend / divisor;
    uint32_t remainder = dividend % divisor;
    if (quotient > 0xFFFFu) {
        s->sr = (uint16_t)((s->sr & ~(M68K_C | M68K_V | M68K_N | M68K_Z)) | M68K_V | M68K_N);
        return;
    }
    s->d[dreg] = (remainder << 16) | quotient;
    set_logic_flags(s, quotient, 2);
}

void m68k_divs(m68k_state *s, m68k_ea src, int dreg)
{
    int32_t divisor = (int16_t)(m68k_get(s, src, 2) & 0xFFFFu);
    int32_t dividend = (int32_t)s->d[dreg];
    s->cycles += divs_cycles(dividend, (int16_t)divisor);
    if (divisor == 0) {
        s->sr = (uint16_t)((s->sr & ~(M68K_C | M68K_V | M68K_N | M68K_Z)) | M68K_V | M68K_N);
        return;
    }
    int32_t quotient = dividend / divisor;
    int32_t remainder = dividend % divisor;
    if (quotient < -32768 || quotient > 32767) {
        s->sr = (uint16_t)((s->sr & ~(M68K_C | M68K_V | M68K_N | M68K_Z)) | M68K_V | M68K_N);
        return;
    }
    s->d[dreg] = ((uint32_t)(remainder & 0xFFFF) << 16) | (uint32_t)(quotient & 0xFFFF);
    set_logic_flags(s, (uint32_t)quotient, 2);
}

void m68k_shift(m68k_state *s, int op, uint32_t count, m68k_ea dst, int size)
{
    dst = resolve_rmw(s, dst, size);
    uint32_t mask = size_mask(size);
    unsigned bits = size_bits(size);
    uint32_t value = m68k_get(s, dst, size) & mask;
    uint32_t result = value;
    int c = 0;
    int old_x = (s->sr & M68K_X) != 0;
    int x = old_x;
    int v = 0;

    if ((op == M68K_SHIFT_ASR || op == M68K_SHIFT_ASL ||
         op == M68K_SHIFT_LSR || op == M68K_SHIFT_LSL))
        count &= 63;
    else
        count &= 63;

    if (count == 0) {
        /* A zero shift clears C and V, leaves X alone, updates N/Z. */
        s->sr = (uint16_t)(s->sr & ~(M68K_C | M68K_V));
        set_nz(s, value, size);
        return;
    }

    switch (op) {
    case M68K_SHIFT_ASL:
        result = (value << count) & mask;
        c = (int)((value >> (bits - (count % bits ? count % bits : bits))) & 1u);
        if (count >= bits) c = 0;
        v = (int)((((value ^ result) >> (bits - 1)) & 1u) != 0);
        x = c;
        break;
    case M68K_SHIFT_ASR: {
        int32_t signed_value = (int32_t)sign_extend(value, size);
        unsigned shift = count > 63 ? 63 : count;
        result = (uint32_t)(signed_value >> shift) & mask;
        c = (int)((signed_value >> (shift - 1)) & 1);
        x = c;
        break;
    }
    case M68K_SHIFT_LSL:
        result = (value << count) & mask;
        c = count >= bits ? 0 : (int)((value >> (bits - count)) & 1u);
        x = c;
        break;
    case M68K_SHIFT_LSR:
        result = count >= bits ? 0 : (value >> count);
        c = count > bits ? 0 : (int)((value >> (count - 1)) & 1u);
        x = c;
        break;
    case M68K_SHIFT_ROL: {
        unsigned n = count % bits;
        result = n ? ((value << n) | (value >> (bits - n))) & mask : value;
        c = (int)(result & 1u);
        break;
    }
    case M68K_SHIFT_ROR: {
        unsigned n = count % bits;
        result = n ? ((value >> n) | (value << (bits - n))) & mask : value;
        c = (int)((result >> (bits - 1)) & 1u);
        break;
    }
    case M68K_SHIFT_ROXL:
        result = value;
        for (uint32_t i = 0; i < count; i++) {
            int new_c = (int)((result >> (bits - 1)) & 1u);
            result = ((result << 1) | (uint32_t)x) & mask;
            x = new_c;
        }
        c = x;
        break;
    case M68K_SHIFT_ROXR:
        result = value;
        for (uint32_t i = 0; i < count; i++) {
            int new_c = (int)(result & 1u);
            result = (result >> 1) | ((uint32_t)x << (bits - 1));
            x = new_c;
        }
        c = x;
        break;
    default:
        break;
    }

    m68k_put(s, dst, size, result);
    set_nz(s, result, size);
    s->sr = (uint16_t)(s->sr & ~(M68K_C | M68K_V | M68K_X));
    if (c) s->sr = (uint16_t)(s->sr | M68K_C);
    if (v) s->sr = (uint16_t)(s->sr | M68K_V);
    if ((op == M68K_SHIFT_ASL || op == M68K_SHIFT_ASR ||
         op == M68K_SHIFT_LSL || op == M68K_SHIFT_LSR ||
         op == M68K_SHIFT_ROXL || op == M68K_SHIFT_ROXR)) {
        if (x) s->sr = (uint16_t)(s->sr | M68K_X);
    } else if (old_x) {
        s->sr = (uint16_t)(s->sr | M68K_X);
    }
}

void m68k_bitop(m68k_state *s, int op, uint32_t bit, m68k_ea dst)
{
    int dreg = dst.mode == M68K_EA_DREG;
    if (!dreg) dst = resolve_rmw(s, dst, 1);
    unsigned bits = dreg ? 32 : 8;
    bit &= bits - 1;
    uint32_t value;
    if (dreg) value = s->d[dst.reg];
    else value = m68k_bus_read8(m68k_ea_address(s, dst));
    int set = (value & (1u << bit)) != 0;
    if (!set) s->sr = (uint16_t)(s->sr | M68K_Z);
    else s->sr = (uint16_t)(s->sr & ~M68K_Z);
    if (op == M68K_BIT_TST) return;
    if (op == M68K_BIT_CHG) value ^= 1u << bit;
    else if (op == M68K_BIT_CLR) value &= ~(1u << bit);
    else value |= 1u << bit;
    if (dreg) s->d[dst.reg] = value;
    else m68k_bus_write8(m68k_ea_address(s, dst), (uint8_t)value);
}

void m68k_movem_to_mem(m68k_state *s, uint16_t mask, m68k_ea dst, int size)
{
    if (dst.mode == M68K_EA_PREDEC) {
        for (int i = 15; i >= 0; --i) {
            if (!(mask & (1u << i))) continue;
            s->a[dst.reg] -= (uint32_t)size;
            uint32_t value = i < 8 ? s->d[i] : s->a[i - 8];
            write_mem(s->a[dst.reg], size, value);
        }
        return;
    }
    uint32_t address = m68k_ea_address(s, dst);
    for (int i = 0; i < 16; ++i) {
        if (!(mask & (1u << i))) continue;
        uint32_t value = i < 8 ? s->d[i] : s->a[i - 8];
        write_mem(address, size, value);
        address += (uint32_t)size;
    }
    if (dst.mode == M68K_EA_POSTINC) s->a[dst.reg] = address;
}

void m68k_movem_to_regs(m68k_state *s, uint16_t mask, m68k_ea src, int size)
{
    uint32_t address = m68k_ea_address(s, src);
    for (int i = 0; i < 16; ++i) {
        if (!(mask & (1u << i))) continue;
        uint32_t value = read_mem(address, size);
        if (size == 2) value = sign_extend(value, 2);
        if (i < 8) s->d[i] = value;
        else s->a[i - 8] = value;
        address += (uint32_t)size;
    }
    if (src.mode == M68K_EA_POSTINC) s->a[src.reg] = address;
}

void m68k_lea(m68k_state *s, m68k_ea src, int areg)
{
    s->a[areg] = m68k_ea_address(s, src);
}

void m68k_pea(m68k_state *s, m68k_ea src)
{
    m68k_push32(s, m68k_ea_address(s, src));
}

void m68k_link(m68k_state *s, int areg, int32_t disp)
{
    m68k_push32(s, s->a[areg]);
    s->a[areg] = s->a[7];
    s->a[7] += (uint32_t)disp;
}

void m68k_unlk(m68k_state *s, int areg)
{
    s->a[7] = s->a[areg];
    s->a[areg] = m68k_pop32(s);
}

void m68k_exg(m68k_state *s, int reg1, int reg2, int both_data)
{
    if (both_data) {
        uint32_t t = s->d[reg1]; s->d[reg1] = s->d[reg2]; s->d[reg2] = t;
    } else if (reg1 < 8 && reg2 >= 8) {
        uint32_t t = s->d[reg1]; s->d[reg1] = s->a[reg2 - 8]; s->a[reg2 - 8] = t;
    } else if (reg1 >= 8 && reg2 < 8) {
        uint32_t t = s->a[reg1 - 8]; s->a[reg1 - 8] = s->d[reg2]; s->d[reg2] = t;
    } else {
        uint32_t t = s->a[reg1 - 8]; s->a[reg1 - 8] = s->a[reg2 - 8]; s->a[reg2 - 8] = t;
    }
}

void m68k_exception(m68k_state *s, unsigned vector)
{
    uint16_t old_sr = s->sr;
    m68k_set_sr(s, (old_sr | M68K_S) & ~M68K_T);   /* to the supervisor stack first */
    m68k_push32(s, s->pc);
    m68k_push16(s, old_sr);
    s->pc = m68k_bus_read32((uint32_t)(vector * 4u));
    s->stopped = 0;
    s->exceptions++;
    s->cycles += 34;
}

void m68k_trap(m68k_state *s, unsigned vector)
{
    m68k_exception(s, 32u + (vector & 15u));
}

void m68k_interrupt(m68k_state *s)
{
    unsigned level = s->irq_level;
    if (level == 0) level = 1;
    uint16_t old_sr = s->sr;
    uint16_t new_sr = (uint16_t)((old_sr | M68K_S) & ~M68K_T);
    new_sr = (uint16_t)((new_sr & ~M68K_IPL) | ((level & 7u) << 8));
    m68k_set_sr(s, new_sr);
    m68k_push32(s, s->pc);
    m68k_push16(s, old_sr);
    s->irq_pending = 0;
    s->cycles += 44;
    /* The interrupt controller supplies the vector and may raise the next
     * pending request (TMP68301 interrupt_callback). */
    uint8_t vector = m68k_irq_acknowledge(s);
    s->pc = m68k_bus_read32((uint32_t)vector * 4u);
    s->stopped = 0;
    s->exceptions++;
}

void m68k_rte(m68k_state *s)
{
    uint16_t sr = m68k_pop16(s);
    s->pc = m68k_pop32(s);
    m68k_set_sr(s, sr);
}

void m68k_rtr(m68k_state *s)
{
    uint16_t ccr = m68k_pop16(s);
    s->pc = m68k_pop32(s);
    m68k_set_ccr(s, ccr);
}

void m68k_stop(m68k_state *s, uint32_t value)
{
    m68k_set_sr(s, value);
    s->stopped = 1;
}

/* ---- less common instructions (MAME m68k semantics) -------------------- */

void m68k_negx(m68k_state *s, m68k_ea dst, int size)
{
    dst = resolve_rmw(s, dst, size);
    uint32_t mask = size_mask(size);
    uint32_t sign = 1u << (size_bits(size) - 1);
    uint32_t value = m68k_get(s, dst, size) & mask;
    uint32_t x = (s->sr & M68K_X) ? 1u : 0u;
    uint32_t result = (0u - value - x) & mask;
    m68k_put(s, dst, size, result);
    int borrow = (value | x) != 0;
    uint16_t sr = (uint16_t)(s->sr & ~(M68K_C | M68K_V | M68K_X | M68K_N));
    if (borrow) sr |= M68K_C | M68K_X;
    if (value & result & sign) sr |= M68K_V;
    if (result & sign) sr |= M68K_N;
    if (result) sr &= (uint16_t)~M68K_Z;
    s->sr = sr;
}

static uint32_t bcd_add(m68k_state *s, uint32_t src, uint32_t dst)
{
    uint32_t x = (s->sr & M68K_X) ? 1u : 0u;
    uint32_t res = (src & 0x0F) + (dst & 0x0F) + x;
    uint32_t v = ~res;
    if (res > 9) res += 6;
    res += (src & 0xF0) + (dst & 0xF0);
    int carry = res > 0x99;
    if (carry) res -= 0xA0;
    v &= res;
    uint16_t sr = (uint16_t)(s->sr & ~(M68K_C | M68K_X | M68K_N | M68K_V));
    if (carry) sr |= M68K_C | M68K_X;
    if (res & 0x80) sr |= M68K_N;
    if (v & 0x80) sr |= M68K_V;
    if (res & 0xFF) sr &= (uint16_t)~M68K_Z;
    s->sr = sr;
    return res & 0xFF;
}

static uint32_t bcd_sub(m68k_state *s, uint32_t src, uint32_t dst)
{
    /* Musashi m68k_op_sbcd semantics. */
    uint32_t x = (s->sr & M68K_X) ? 1u : 0u;
    uint32_t res = (dst & 0x0F) - (src & 0x0F) - x;
    uint32_t corf = res > 9 ? 6u : 0u;
    res += (dst & 0xF0) - (src & 0xF0);
    uint32_t v = res;
    int carry = res > 0x99;
    if (carry) res += 0xA0;
    res -= corf;
    v &= ~res;
    uint16_t sr = (uint16_t)(s->sr & ~(M68K_C | M68K_X | M68K_N | M68K_V));
    if (carry) sr |= M68K_C | M68K_X;
    if (res & 0x80) sr |= M68K_N;
    if (v & 0x80) sr |= M68K_V;
    if (res & 0xFF) sr &= (uint16_t)~M68K_Z;
    s->sr = sr;
    return res & 0xFF;
}

void m68k_abcd(m68k_state *s, m68k_ea src, m68k_ea dst)
{
    uint32_t a = m68k_get(s, src, 1);
    dst = resolve_rmw(s, dst, 1);
    uint32_t b = m68k_get(s, dst, 1);
    m68k_put(s, dst, 1, bcd_add(s, a, b));
}

void m68k_sbcd(m68k_state *s, m68k_ea src, m68k_ea dst)
{
    uint32_t a = m68k_get(s, src, 1);
    dst = resolve_rmw(s, dst, 1);
    uint32_t b = m68k_get(s, dst, 1);
    m68k_put(s, dst, 1, bcd_sub(s, a, b));
}

void m68k_nbcd(m68k_state *s, m68k_ea dst)
{
    /* Musashi m68k_op_nbcd semantics. */
    dst = resolve_rmw(s, dst, 1);
    uint32_t d = m68k_get(s, dst, 1);
    uint32_t x = (s->sr & M68K_X) ? 1u : 0u;
    uint32_t res = (0x9Au - d - x) & 0xFFu;
    uint16_t sr = (uint16_t)(s->sr & ~(M68K_C | M68K_X | M68K_N | M68K_V));
    if (res != 0x9A) {
        uint32_t v = ~res;
        if ((res & 0x0F) == 0x0A) res = (res & 0xF0) + 0x10;
        res &= 0xFF;
        v &= res;
        m68k_put(s, dst, 1, res);
        if (res) sr &= (uint16_t)~M68K_Z;
        sr |= M68K_C | M68K_X;
        if (v & 0x80) sr |= M68K_V;
    }
    if (res & 0x80) sr |= M68K_N;
    s->sr = sr;
}

void m68k_movep(m68k_state *s, int areg, int32_t disp, int dreg, int size, int to_memory)
{
    uint32_t address = s->a[areg] + (uint32_t)disp;
    int bytes = size == 4 ? 4 : 2;
    if (to_memory) {
        uint32_t v = s->d[dreg];
        for (int i = 0; i < bytes; i++)
            m68k_bus_write8(address + (uint32_t)(i * 2), (uint8_t)(v >> (8 * (bytes - 1 - i))));
    } else {
        uint32_t v = 0;
        for (int i = 0; i < bytes; i++) v = (v << 8) | m68k_bus_read8(address + (uint32_t)(i * 2));
        if (bytes == 2) s->d[dreg] = (s->d[dreg] & 0xFFFF0000u) | v;
        else s->d[dreg] = v;
    }
}

static uint32_t unsupported_pc;
uint32_t m68k_unsupported(m68k_state *s, uint32_t pc)
{
    (void)s;
    unsupported_pc = pc;
    fprintf(stderr, "reached an instruction the runtime does not model at %06X\n", pc);
    return 0xFFFFFFFFu;
}
