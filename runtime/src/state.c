/* Save-state serialization (see cy/state.h).  One function walks the board
 * for both directions so the save and load layouts cannot drift apart. */
#include "ga/state.h"
#include "m68k/program_api.h"
#include "miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t ga_crc32(uint32_t crc, const void *data, size_t size)
{
    return (uint32_t)mz_crc32(crc, (const unsigned char *)data, size);
}

uint32_t ga_board_rom_fingerprint(const ga_board *b)
{
    static const uint8_t *cached_program;
    static uint32_t cached;
    if (cached && cached_program == b->program) return cached;
    uint32_t crc = ga_crc32(0, b->program, 0x100000);
    crc = ga_crc32(crc, b->sprite_gfx, 0x1000000);
    for (int l = 0; l < 3; l++) crc = ga_crc32(crc, b->layer_gfx[l], 0x400000);
    crc = ga_crc32(crc, b->samples, 0xC00000);
    cached_program = b->program;
    cached = crc ? crc : 1;
    return cached;
}

/* ---- stream ----------------------------------------------------------- */

typedef struct io {
    int writing;
    uint8_t *p;           /* write buffer, or read source */
    size_t n, cap, at;
    int bad;
} io;

static void io_bytes(io *s, void *v, size_t n)
{
    if (s->bad) { if (!s->writing) memset(v, 0, n); return; }
    if (s->writing) {
        if (s->n + n > s->cap) {
            size_t cap = s->cap ? s->cap : 1u << 18;
            while (cap < s->n + n) cap *= 2;
            uint8_t *p = (uint8_t *)realloc(s->p, cap);
            if (!p) { s->bad = 1; return; }
            s->p = p; s->cap = cap;
        }
        memcpy(s->p + s->n, v, n);
        s->n += n;
    } else {
        if (s->at + n > s->n) { s->bad = 1; memset(v, 0, n); return; }
        memcpy(v, s->p + s->at, n);
        s->at += n;
    }
}
static void io_u8(io *s, uint8_t *v) { io_bytes(s, v, 1); }
static void io_u16(io *s, uint16_t *v)
{
    uint8_t b[2] = { (uint8_t)*v, (uint8_t)(*v >> 8) };
    io_bytes(s, b, 2);
    if (!s->writing) *v = (uint16_t)(b[0] | b[1] << 8);
}
static void io_u32(io *s, uint32_t *v)
{
    uint8_t b[4] = { (uint8_t)*v, (uint8_t)(*v >> 8), (uint8_t)(*v >> 16), (uint8_t)(*v >> 24) };
    io_bytes(s, b, 4);
    if (!s->writing) *v = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static void io_u64(io *s, uint64_t *v)
{
    uint32_t lo = (uint32_t)*v, hi = (uint32_t)(*v >> 32);
    io_u32(s, &lo); io_u32(s, &hi);
    if (!s->writing) *v = (uint64_t)hi << 32 | lo;
}
static void io_i32(io *s, int32_t *v) { uint32_t u = (uint32_t)*v; io_u32(s, &u); *v = (int32_t)u; }
static void io_int(io *s, int *v) { uint32_t u = (uint32_t)*v; io_u32(s, &u); *v = (int)(int32_t)u; }
static void io_u16a(io *s, uint16_t *a, size_t count) { for (size_t i = 0; i < count; i++) io_u16(s, &a[i]); }

/* Sections: 4-byte tag + 32-bit length; on load the tag and length must match. */
static size_t sec_begin(io *s, const char tag[4])
{
    char t[4];
    memcpy(t, tag, 4);
    io_bytes(s, t, 4);
    if (!s->writing && memcmp(t, tag, 4) != 0) s->bad = 2;
    size_t at = s->writing ? s->n : s->at;
    uint32_t len = 0;
    io_u32(s, &len);
    if (!s->writing) return (size_t)len | ((size_t)1 << 62);   /* remember the claimed length */
    return at;
}
static void sec_end(io *s, size_t mark, size_t start)
{
    if (s->bad) return;
    if (s->writing) {
        uint32_t len = (uint32_t)(s->n - mark - 4);
        memcpy(s->p + mark, &len, 4);   /* little-endian hosts only (x86-64, ARM64) */
    } else {
        size_t claimed = mark & ~((size_t)1 << 62);
        if (s->at - start != claimed) s->bad = 2;
    }
}
#define SECTION(tag, ...) do { size_t m_ = sec_begin(s, tag); size_t st_ = s->writing ? 0 : s->at; __VA_ARGS__; sec_end(s, m_, st_); } while (0)

static void io_i16(io *s, int16_t *v) { uint16_t u = (uint16_t)*v; io_u16(s, &u); *v = (int16_t)u; }

static void io_m68k(io *s, m68k_state *c)
{
    for (int i = 0; i < 8; i++) io_u32(s, &c->d[i]);
    for (int i = 0; i < 8; i++) io_u32(s, &c->a[i]);
    io_u32(s, &c->pc);
    io_u16(s, &c->sr);
    io_u8(s, &c->stopped); io_u8(s, &c->irq_pending); io_u8(s, &c->irq_level); io_u8(s, &c->irq_vector);
    io_u64(s, &c->instructions); io_u64(s, &c->exceptions); io_u64(s, &c->cycles);
    io_u32(s, &c->other_sp);
}

static void io_ymz(io *s, ymz280b *z)
{
    io_u8(s, &z->current_register); io_u8(s, &z->status_register); io_u8(s, &z->irq_state); io_u8(s, &z->irq_mask);
    io_u8(s, &z->irq_enable); io_u8(s, &z->keyon_enable); io_u8(s, &z->ext_mem_enable); io_u8(s, &z->ext_readlatch);
    io_u32(s, &z->ext_mem_address_hi); io_u32(s, &z->ext_mem_address_mid); io_u32(s, &z->ext_mem_address);
    for (int i = 0; i < 8; i++) {
        ymz280b_voice *v = &z->voice[i];
        io_u8(s, &v->playing); io_u8(s, &v->ended); io_u8(s, &v->keyon); io_u8(s, &v->looping); io_u8(s, &v->mode);
        io_u16(s, &v->fnum); io_u8(s, &v->level); io_u8(s, &v->pan);
        io_u32(s, &v->start); io_u32(s, &v->stop); io_u32(s, &v->loop_start); io_u32(s, &v->loop_end); io_u32(s, &v->position);
        io_i32(s, &v->signal); io_i32(s, &v->step); io_i32(s, &v->loop_signal); io_i32(s, &v->loop_step);
        io_u32(s, &v->loop_count);
        io_i32(s, &v->output_left); io_i32(s, &v->output_right); io_i32(s, &v->output_step); io_i32(s, &v->output_pos);
        io_i16(s, &v->last_sample); io_i16(s, &v->curr_sample); io_u8(s, &v->irq_schedule);
    }
}

static void io_sprite(io *s, ga_sprite *p)
{
    int32_t *f[] = { &p->priority, &p->flags, &p->line_offset, &p->tile_width, &p->tile_height, &p->total_width,
                     &p->total_height, &p->x, &p->y, &p->xcount0, &p->ycount0, &p->zoomx_re, &p->zoomy_re };
    for (size_t i = 0; i < sizeof(f) / sizeof(f[0]); i++) io_i32(s, f[i]);
    io_u32(s, &p->pen_data); io_u32(s, &p->base_pen);
}

static void io_board(io *s, ga_board *b)
{
    SECTION("M68K", io_m68k(s, &b->cpu));
    SECTION("RAM ", io_bytes(s, b->ram, sizeof(b->ram)));
    SECTION("VID ", {
        io_bytes(s, b->spriteram, sizeof(b->spriteram));
        io_bytes(s, b->vram, sizeof(b->vram));
        io_u16a(s, b->videoregs, 0x40);
        for (int l = 0; l < 3; l++) io_u16a(s, b->layer_vregs[l], 3);
        io_bytes(s, b->palette_ram, sizeof(b->palette_ram));
        for (size_t i = 0; i < 0x8000; i++) io_u32(s, &b->pens[i]);
        io_bytes(s, b->tiledim, 3);
        io_i32(s, &b->spriteram_bank);
        io_i32(s, &b->num_sprites);
        for (int i = 0; i < GA_MAX_SPRITES; i++) io_sprite(s, &b->sprites[i]);
        io_u16a(s, b->zbuf, sizeof(b->zbuf) / 2);
        io_u16(s, &b->zbuf_baseval);
        for (size_t i = 0; i < sizeof(b->screen) / 4; i++) io_u32(s, &b->screen[i]);
    });
    SECTION("IRQ ", { io_u8(s, &b->vblank_irq); io_u8(s, &b->sound_irq); io_u8(s, &b->unknown_irq); io_u8(s, &b->agallet_vblank_irq); });
    SECTION("YMZ ", io_ymz(s, &b->ymz));
    SECTION("MISC", {
        io_u16(s, &b->dsw);
        io_u64(s, &b->lines_run); io_u16(s, &b->vpos); io_u64(s, &b->frames_run);
        io_u64(s, &b->next_vblank_irq); io_u64(s, &b->next_vblank_end); io_u64(s, &b->next_sound_update);
        io_u64(s, &b->audio_samples);
    });
}

uint8_t *ga_state_save(const ga_board *b, size_t *size)
{
    io s = { 1, NULL, 0, 0, 0, 0 };
    uint32_t version = GA_STATE_VERSION, fp = ga_board_rom_fingerprint(b), entries = m68k_program_entry_count();
    io_bytes(&s, (void *)GA_STATE_MAGIC, 8);
    io_u32(&s, &version); io_u32(&s, &fp); io_u32(&s, &entries);
    io_board(&s, (ga_board *)b);       /* writing does not modify the board */
    uint32_t crc = s.bad ? 0 : ga_crc32(0, s.p, s.n);
    io_u32(&s, &crc);
    if (s.bad) { free(s.p); return NULL; }
    *size = s.n;
    return s.p;
}

static int fail(char *err, size_t n, const char *m) { if (err && n) snprintf(err, n, "%s", m); return 0; }

/* Parse into a scratch board so the real one is untouched on failure. */
static ga_board *parse(const ga_board *b, const uint8_t *data, size_t size, char *err, size_t n)
{
    if (!data || size < 8 + 12 + 4) { fail(err, n, "state file is truncated"); return NULL; }
    if (memcmp(data, GA_STATE_MAGIC, 8) != 0) { fail(err, n, "not a Gaia Crusaders save state"); return NULL; }
    uint32_t stored = (uint32_t)data[size - 4] | (uint32_t)data[size - 3] << 8 | (uint32_t)data[size - 2] << 16 | (uint32_t)data[size - 1] << 24;
    if (ga_crc32(0, data, size - 4) != stored) { fail(err, n, "state is damaged (checksum mismatch)"); return NULL; }
    io s = { 0, (uint8_t *)data, size - 4, 0, 8, 0 };
    uint32_t version = 0, fp = 0, entries = 0;
    io_u32(&s, &version); io_u32(&s, &fp); io_u32(&s, &entries);
    if (version != GA_STATE_VERSION) {
        char m[96]; snprintf(m, sizeof(m), "state format version %u is not supported (expected %u)", version, GA_STATE_VERSION);
        fail(err, n, m); return NULL;
    }
    if (fp != ga_board_rom_fingerprint(b)) { fail(err, n, "state was made with a different ROM set"); return NULL; }
    ga_board *t = (ga_board *)malloc(sizeof(*t));
    if (!t) { fail(err, n, "out of memory"); return NULL; }
    memcpy(t, b, sizeof(*t));
    io_board(&s, t);
    if (s.bad || s.at != s.n) { free(t); fail(err, n, "state has an unexpected layout or is truncated"); return NULL; }
    /* no check on the resume address: code the build did not translate runs
     * through the fallback interpreter, so states move freely between the
     * recompiled and the generic build of the same ROM set */
    return t;
}

int ga_state_validate(const ga_board *b, const uint8_t *data, size_t size, char *err, size_t n)
{
    ga_board *t = parse(b, data, size, err, n);
    if (!t) return 0;
    free(t);
    return 1;
}

int ga_state_load(ga_board *b, const uint8_t *data, size_t size, char *err, size_t n)
{
    ga_board *t = parse(b, data, size, err, n);
    if (!t) return 0;
    uint32_t inputs = 0;
    memcpy(b, t, sizeof(*b));
    free(t);
    
    b->inputs = inputs;   /* inputs held when the state was made must not stay latched */
    ga_board_attach(b);
    return 1;
}
