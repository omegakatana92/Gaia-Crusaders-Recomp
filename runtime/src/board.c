/* Gaia Crusaders board: ROM loading and checking, the 68000 bus (MAME
 * cave.cpp gaia_map), the Cave interrupt logic and the frame scheduler. */
#include "ga/board.h"
#include "board/bus.h"
#include "m68k/runner.h"
#include "miniz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#else
#define strcasecmp _stricmp
#endif

typedef struct rom_entry { const char *name; uint32_t size, crc; const char *role; } rom_entry;

static const rom_entry ROM_FILES[] = {
    { "prg1.127", 0x80000, 0x47B904B2, "68000 program (even)" },
    { "prg2.128", 0x80000, 0x469B7794, "68000 program (odd)" },
    { "obj1.736", 0x400000, 0xF4F84E5D, "sprites" },
    { "obj2.738", 0x400000, 0x15C2A9CE, "sprites" },
    { "bg1.989", 0x400000, 0x013A693D, "layer 0 tiles" },
    { "bg2.995", 0x400000, 0x783CC62F, "layer 1 tiles" },
    { "bg3.998", 0x400000, 0xBCD61D1C, "layer 2 tiles" },
    { "snd1.447", 0x400000, 0x92770A52, "YMZ280B samples" },
    { "snd2.454", 0x400000, 0x329AE1CF, "YMZ280B samples" },
    { "snd3.455", 0x400000, 0x4048D64E, "YMZ280B samples" },
};
#define ROM_COUNT (sizeof(ROM_FILES) / sizeof(ROM_FILES[0]))
#define SAMPLE_ROM_SIZE 0xC00000u

static ga_board *g;
static FILE *ymz_trace;   /* GA_YMZ_TRACE=file: development trace of YMZ280B accesses */
static void trace_ymz(const ga_board *b, char rw, unsigned off, unsigned v)
{
    static int init;
    if (!init) { const char *p = getenv("GA_YMZ_TRACE"); init = 1; if (p) ymz_trace = fopen(p, "w"); }
    if (ymz_trace) fprintf(ymz_trace, "%llu %c %u %02X %.9f\n", (unsigned long long)b->frames_run + 1, rw, off, v & 0xFF, (double)b->cpu.cycles / 16000000.0);
}

/* --------------------------------------------------------------- ROMs */

/* archives may keep the files in a folder (e.g. gaia/prg1.127) */
static int zip_find(mz_zip_archive *z, const char *name)
{
    int n = (int)mz_zip_reader_get_num_files(z);
    for (int i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(z, (mz_uint)i, &st) || st.m_is_directory) continue;
        const char *base = st.m_filename, *p;
        for (p = st.m_filename; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
        if (!strcasecmp(base, name)) return i;
    }
    return -1;
}

static int open_zip(mz_zip_archive *z, const char *path)
{
    memset(z, 0, sizeof(*z));
    return path && *path && mz_zip_reader_init_file(z, path, 0);
}

int ga_rom_check(const char *zip_path, char *err, size_t n)
{
    mz_zip_archive z;
    if (!open_zip(&z, zip_path)) { snprintf(err, n, "Cannot open the archive \"%s\".", zip_path ? zip_path : ""); return 0; }
    if (zip_find(&z, "t-h.u127") >= 0) {
        snprintf(err, n, "This is Thunder Heroes (theroes), not Gaia Crusaders.");
        mz_zip_reader_end(&z);
        return 0;
    }
    char missing[300] = "", bad[300] = "";
    for (size_t i = 0; i < ROM_COUNT; i++) {
        int idx = zip_find(&z, ROM_FILES[i].name);
        mz_zip_archive_file_stat st;
        char *list = NULL;
        if (idx < 0 || !mz_zip_reader_file_stat(&z, (mz_uint)idx, &st)) list = missing;
        else if (st.m_uncomp_size != ROM_FILES[i].size || st.m_crc32 != ROM_FILES[i].crc) list = bad;
        if (list) {
            strncat(list, " ", 299 - strlen(list));
            strncat(list, ROM_FILES[i].name, 299 - strlen(list));
        }
    }
    mz_zip_reader_end(&z);
    if (missing[0] || bad[0]) {
        snprintf(err, n, "Not the MAME 'gaia' set.%s%s%s%s", missing[0] ? " Missing:" : "", missing,
                 bad[0] ? " Wrong size/CRC:" : "", bad);
        return 0;
    }
    return 1;
}

static uint8_t *extract(mz_zip_archive *z, const char *name)
{
    int idx = zip_find(z, name);
    if (idx < 0) return NULL;
    size_t size = 0;
    return (uint8_t *)mz_zip_reader_extract_to_heap(z, (mz_uint)idx, &size, 0);
}

int ga_board_load(ga_board *b, const char *zip_path)
{
    memset(b, 0, sizeof(*b));
    if (!ga_rom_check(zip_path, b->error, sizeof(b->error))) return 0;
    mz_zip_archive z;
    open_zip(&z, zip_path);
    uint8_t *f[ROM_COUNT];
    int ok = 1;
    for (size_t i = 0; i < ROM_COUNT; i++) { f[i] = extract(&z, ROM_FILES[i].name); ok &= f[i] != NULL; }
    mz_zip_reader_end(&z);
    b->program = (uint8_t *)calloc(1, 0x100000);
    b->samples = (uint8_t *)malloc(SAMPLE_ROM_SIZE);
    uint8_t *sprite_rom = (uint8_t *)malloc(0x800000);
    if (!ok || !b->program || !b->samples || !sprite_rom) {
        snprintf(b->error, sizeof(b->error), "Could not read the ROM files (out of memory or damaged archive).");
        for (size_t i = 0; i < ROM_COUNT; i++) mz_free(f[i]);
        free(sprite_rom);
        return 0;
    }
    for (size_t i = 0; i < 0x80000; i++) { b->program[i * 2] = f[0][i]; b->program[i * 2 + 1] = f[1][i]; }  /* ROM_LOAD16_BYTE */
    memcpy(sprite_rom, f[2], 0x400000);
    memcpy(sprite_rom + 0x400000, f[3], 0x400000);
    memcpy(b->samples, f[7], 0x400000);
    memcpy(b->samples + 0x400000, f[8], 0x400000);
    memcpy(b->samples + 0x800000, f[9], 0x400000);
    const uint8_t *layers[3] = { f[4], f[5], f[6] };
    ga_video_decode(b, sprite_rom, layers);
    free(sprite_rom);
    for (size_t i = 0; i < ROM_COUNT; i++) mz_free(f[i]);
    if (!b->sprite_gfx || !b->layer_gfx[0] || !b->layer_gfx[1] || !b->layer_gfx[2]) {
        snprintf(b->error, sizeof(b->error), "out of memory");
        return 0;
    }
    b->dsw = GA_DSW_DEFAULT;
    ga_board_reset(b);
    return 1;
}

void ga_board_free(ga_board *b)
{
    free(b->program); free(b->samples); free(b->sprite_gfx);
    for (int i = 0; i < 3; i++) free(b->layer_gfx[i]);
    b->program = b->samples = b->sprite_gfx = NULL;
    for (int i = 0; i < 3; i++) b->layer_gfx[i] = NULL;
}

void ga_board_attach(ga_board *b)
{
    g = b;
    b->ymz.rom = b->samples;
    b->ymz.rom_size = SAMPLE_ROM_SIZE;
}

void ga_board_set_inputs(ga_board *b, uint32_t m) { b->inputs = m; }

/* ---------------------------------------------------------- interrupts */

/* update_irq_state: one level-1 line shared by VBLANK, the YMZ280B and the
 * "unknown" source; it stays asserted until the cause is cleared. */
static void update_irq(ga_board *b)
{
    int on = b->vblank_irq || b->sound_irq || b->unknown_irq;
    b->cpu.irq_pending = (uint8_t)on;
    b->cpu.irq_level = on ? 1 : 0;
    b->cpu.irq_vector = 25;
}

uint8_t m68k_irq_acknowledge(m68k_state *s)
{
    (void)s;
    update_irq(g);    /* HOLD is not used: the line stays up until irq_cause_r / the YMZ status clear it */
    return 24 + 1;    /* autovector */
}

static uint16_t irq_cause_r(ga_board *b, unsigned offset)
{
    uint16_t result = 0x0003;
    if (b->vblank_irq) result ^= 0x01;
    if (b->unknown_irq) result ^= 0x02;
    if (offset == 2) b->vblank_irq = 0;
    if (offset == 3) b->unknown_irq = 0;
    update_irq(b);
    if (offset == 0) {
        result &= (uint16_t)~4;
        result |= b->agallet_vblank_irq ? 0 : 4;
    }
    return result;
}

static int32_t frame_L[GA_MAX_FRAME_SAMPLES + 8], frame_R[GA_MAX_FRAME_SAMPLES + 8];
static int frame_produced;

/* Bring the YMZ280B up to the CPU's current time (MAME updates the sound
 * stream before every register access).  The chip and the 68000 share the
 * 16 MHz clock, so one output sample is exactly 192 CPU cycles. */
static void sync_audio(ga_board *b, int service)
{
    uint64_t target = b->cpu.cycles / 192u;
    uint64_t n = target > b->audio_samples ? target - b->audio_samples : 0;
    if (n) b->audio_samples = target;
    while (n > 0) {
        int chunk = n > 512 ? 512 : (int)n;
        int room = GA_MAX_FRAME_SAMPLES - frame_produced;
        if (room > 0) {
            int c = chunk < room ? chunk : room;
            ymz280b_render(&b->ymz, &frame_L[frame_produced], &frame_R[frame_produced], c);
            frame_produced += c;
            if (c < chunk) { static int32_t dl[512], dr[512]; ymz280b_render(&b->ymz, dl, dr, chunk - c); }
        } else {
            static int32_t dl[512], dr[512];
            ymz280b_render(&b->ymz, dl, dr, chunk);
        }
        n -= (uint64_t)chunk;
    }
    /* MAME notices a finished sample only when it updates the stream (at a
     * register access or its 50 Hz sound update) and raises the status bit on
     * a zero-delay timer, i.e. after the CPU's current instruction: a status
     * read that triggered the update does not see it yet.  Our extra
     * per-scanline rendering keeps such ends pending until one of those points. */
    if (service) ymz280b_service(&b->ymz);
}

static void sound_irq_update(ga_board *b)
{
    int s = ymz280b_irq(&b->ymz);
    if (s != b->sound_irq) { b->sound_irq = (uint8_t)s; update_irq(b); }
}

/* ----------------------------------------------------------------- bus */

static inline uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline void put16(uint8_t *p, uint16_t v, uint16_t mask)
{
    if (mask & 0xFF00) p[0] = (uint8_t)(v >> 8);
    if (mask & 0x00FF) p[1] = (uint8_t)v;
}

static uint16_t in0(const ga_board *b) { return (uint16_t)~(b->inputs & 0xFFFF); }
static uint16_t in1(const ga_board *b)
{
    uint16_t v = 0xFFFF;
    if (b->inputs & GA_IN_COIN1) v &= (uint16_t)~0x01;
    if (b->inputs & GA_IN_COIN2) v &= (uint16_t)~0x02;
    if (b->inputs & GA_IN_TEST) v &= (uint16_t)~0x04;
    if (b->inputs & GA_IN_SERVICE1) v &= (uint16_t)~0x08;
    if (b->inputs & GA_IN_P1_START) v &= (uint16_t)~0x10;
    if (b->inputs & GA_IN_P2_START) v &= (uint16_t)~0x20;
    return v;
}

static uint16_t read_word(uint32_t a)
{
    ga_board *b = g;
    a &= 0xFFFFFE;
    if (a < 0x100000) return be16(&b->program[a]);
    if (a >= 0x100000 && a < 0x110000) return be16(&b->ram[a - 0x100000]);
    if (a >= 0x300000 && a < 0x300004) {
        sync_audio(b, 0);
        uint8_t v = ymz280b_read(&b->ymz, (a >> 1) & 1);
        trace_ymz(b, 'R', (a >> 1) & 1, v);
        ymz280b_service(&b->ymz);
        sound_irq_update(b);
        return v;
    }
    if (a >= 0x400000 && a < 0x410000) return be16(&b->spriteram[a - 0x400000]);
    if (a >= 0x500000 && a < 0x800000) {
        unsigned layer = (a >> 20) - 5;
        return be16(&b->vram[layer][a & 0xFFFF]);
    }
    if (a >= 0x800000 && a < 0x800008) return irq_cause_r(b, (a & 7) >> 1);
    if (a >= 0x900000 && a < 0xC00000 && (a & 0xFFFFF) < 6) return b->layer_vregs[(a >> 20) - 9][(a & 7) >> 1];
    if (a >= 0xC00000 && a < 0xC10000) return be16(&b->palette_ram[a - 0xC00000]);
    if (a == 0xD00010) return in0(b);
    if (a == 0xD00012) return in1(b);
    if (a == 0xD00014) return b->dsw;
    return 0;   /* unmapped */
}

static void write_word(uint32_t a, uint16_t d, uint16_t mask)
{
    ga_board *b = g;
    a &= 0xFFFFFE;
    if (a >= 0x100000 && a < 0x110000) { put16(&b->ram[a - 0x100000], d, mask); return; }
    if (a >= 0x300000 && a < 0x300004) {   /* umask16(0x00ff): the low byte lane */
        if (mask & 0x00FF) {
            sync_audio(b, 0);
            trace_ymz(b, 'W', (a >> 1) & 1, d);
            ymz280b_write(&b->ymz, (a >> 1) & 1, (uint8_t)d);
            ymz280b_service(&b->ymz);
            sound_irq_update(b);
        }
        return;
    }
    if (a >= 0x400000 && a < 0x410000) { put16(&b->spriteram[a - 0x400000], d, mask); return; }
    if (a >= 0x500000 && a < 0x800000) {
        unsigned layer = (a >> 20) - 5;
        put16(&b->vram[layer][a & 0xFFFF], d, mask);
        return;
    }
    if (a >= 0x800000 && a < 0x800080) {
        uint16_t *r = &b->videoregs[(a & 0x7F) >> 1];
        *r = (uint16_t)((*r & ~mask) | (d & mask));
        return;
    }
    if (a >= 0x900000 && a < 0xC00000 && (a & 0xFFFFF) < 6) {
        uint16_t *r = &b->layer_vregs[(a >> 20) - 9][(a & 7) >> 1];
        *r = (uint16_t)((*r & ~mask) | (d & mask));
        return;
    }
    if (a >= 0xC00000 && a < 0xC10000) {
        put16(&b->palette_ram[a - 0xC00000], d, mask);
        ga_video_palette_write(b, (a - 0xC00000) >> 1);
        return;
    }
    /* 0xD00011 coin counters, 0xD00014 watchdog (disabled in MAME): no effect */
}

uint16_t m68k_bus_read16(uint32_t a) { return read_word(a); }
uint8_t m68k_bus_read8(uint32_t a)
{
    a &= 0xFFFFFF;
    /* byte reads of the YMZ280B only touch its byte lane (status reads clear it) */
    if (a >= 0x300000 && a < 0x300004 && !(a & 1)) return 0;
    uint16_t w = read_word(a);
    return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
}
uint32_t m68k_bus_read32(uint32_t a) { return (uint32_t)read_word(a) << 16 | read_word(a + 2); }
void m68k_bus_write16(uint32_t a, uint16_t d) { write_word(a, d, 0xFFFF); }
void m68k_bus_write8(uint32_t a, uint8_t d)
{
    if (a & 1) write_word(a, d, 0x00FF);
    else write_word(a, (uint16_t)(d << 8), 0xFF00);
}
void m68k_bus_write32(uint32_t a, uint32_t d) { write_word(a, (uint16_t)(d >> 16), 0xFFFF); write_word(a + 2, (uint16_t)d, 0xFFFF); }

/* ----------------------------------------------------------- scheduler */

#define VBLANK_IRQ_DELAY (16u * (17376u - 2000u))   /* m_time_vblank_irq = 2000 us before the next VBLANK */
#define VBLANK_END_DELAY (16u * 2000u)
#define SOUND_UPDATE_PERIOD (GA_CPU_CLOCK / 50u)   /* STREAMS_UPDATE_FREQUENCY */

void ga_board_reset(ga_board *b)
{
    ga_board_attach(b);
    memset(b->ram, 0, sizeof(b->ram));
    memset(b->spriteram, 0, sizeof(b->spriteram));
    memset(b->vram, 0, sizeof(b->vram));
    memset(b->videoregs, 0, sizeof(b->videoregs));
    memset(b->layer_vregs, 0, sizeof(b->layer_vregs));
    memset(b->palette_ram, 0, sizeof(b->palette_ram));
    memset(b->pens, 0, sizeof(b->pens));
    memset(b->tiledim, 0, sizeof(b->tiledim));
    b->spriteram_bank = 0;
    b->num_sprites = 0;
    memset(b->zbuf, 0, sizeof(b->zbuf));
    b->zbuf_baseval = (uint16_t)(0x10000 - GA_MAX_SPRITES);
    memset(b->screen, 0, sizeof(b->screen));
    b->vblank_irq = b->sound_irq = b->unknown_irq = b->agallet_vblank_irq = 0;
    ymz280b_reset(&b->ymz);
    b->lines_run = 0;
    b->vpos = 0;
    if (b->frame_end_line >= GA_LINES) b->frame_end_line = 0;   /* frames end at VBLANK (see below) */
    b->frames_run = 0;
    b->next_vblank_irq = b->next_vblank_end = UINT64_MAX;
    b->next_sound_update = SOUND_UPDATE_PERIOD;
    b->audio_samples = 0;
    memset(&b->cpu, 0, sizeof(b->cpu));
    b->cpu.sr = 0x2700;
    b->cpu.a[7] = m68k_bus_read32(0);
    b->cpu.pc = m68k_bus_read32(4) & 0xFFFFFF;
    update_irq(b);
}

static uint64_t line_cycles(uint64_t lines) { return lines * GA_CYCLES_PER_FRAME / GA_LINES; }

/* Fire the timer events that are due at the CPU's current time. */
static void timers(ga_board *b)
{
    if (b->cpu.cycles >= b->next_vblank_irq) {
        /* vblank_start: raise the VBLANK cause and latch the sprite list */
        uint64_t t = b->next_vblank_irq;
        b->next_vblank_irq = UINT64_MAX;
        b->vblank_irq = 1;
        update_irq(b);
        ga_video_latch_sprites(b);
        b->agallet_vblank_irq = 1;
        b->next_vblank_end = t + VBLANK_END_DELAY;
    }
    if (b->cpu.cycles >= b->next_vblank_end) {
        b->next_vblank_end = UINT64_MAX;
        b->agallet_vblank_irq = 0;
    }
    if (b->cpu.cycles >= b->next_sound_update) {
        b->next_sound_update += SOUND_UPDATE_PERIOD;
        sync_audio(b, 1);
        sound_irq_update(b);
    }
}

int ga_board_run_frame(ga_board *b, int16_t *stereo, int *frames)
{
    ga_board_attach(b);
    int status = GA_RUN_OK;
    frame_produced = 0;

    do {
        int line = b->vpos;
        b->lines_run++;
        uint64_t end = line_cycles(b->lines_run);
        while (status == GA_RUN_OK && b->cpu.cycles < end) {
            uint64_t next = end;
            if (b->next_vblank_irq < next) next = b->next_vblank_irq;
            if (b->next_vblank_end < next) next = b->next_vblank_end;
            if (b->next_sound_update < next) next = b->next_sound_update;
            if (m68k_run_until(&b->cpu, next) != M68K_RUN_OK) { status = GA_RUN_MISSING; break; }
            timers(b);
        }
        sync_audio(b, 0);
        b->vpos = (uint16_t)((line + 1) % GA_LINES);
        if (b->vpos == 0) {
            /* VBLANK: the screen is drawn (screen_update) and interrupt()
             * schedules the VBLANK interrupt 2000 us before the next one.
             * MAME's old-style screen (vblank time 0, 240 lines) starts VBLANK
             * at the frame boundary, not after the 224 visible lines: measured
             * against MAME 0.264, the game's accesses line up only this way. */
            ga_video_update(b);
            b->next_vblank_irq = line_cycles(b->lines_run) + VBLANK_IRQ_DELAY;
        }
    } while (b->vpos != b->frame_end_line && status == GA_RUN_OK);

    /* mono speaker: both YMZ280B outputs summed, as MAME routes ALL_OUTPUTS to "mono" */
    for (int i = 0; i < frame_produced; i++) {
        int32_t m = (frame_L[i] + frame_R[i]) / 512;
        if (m > 32767) m = 32767; else if (m < -32768) m = -32768;
        if (stereo) { stereo[i * 2] = (int16_t)m; stereo[i * 2 + 1] = (int16_t)m; }
    }
    if (frames) *frames = frame_produced;
    b->frames_run++;
    return status;
}
