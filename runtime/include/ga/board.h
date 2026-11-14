/* Cave 68000 board as Gaia Crusaders wires it (Noise Factory, 1999).
 *
 * Reference: MAME 0.264 src/mame/atlus/cave.cpp (gaia_map, gaia machine
 * config, interrupt timing), cave_v.cpp (sprites, screen update),
 * src/devices/video/tmap038.cpp and src/devices/sound/ymz280b.cpp, all
 * BSD-3-Clause.  Behaviour only; no MAME code is linked. */
#ifndef GA_BOARD_H
#define GA_BOARD_H

#include <stddef.h>
#include <stdint.h>
#include "m68k/m68k.h"
#include "ymz/ymz280b.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Screen: MAME's old-style screen of 320x240 lines at 15625/271.5 Hz with the
 * visible area cut to 320x224; VBLANK (picture, interrupt timing) is at the
 * 240-line frame boundary, as MAME runs it. */
#define GA_SCREEN_W 320
#define GA_SCREEN_H 224
#define GA_SCREEN_TOTAL_H 240                 /* screen height used for sprite flipping */
#define GA_LINES 240
#define GA_REFRESH (15625.0 / 271.5)          /* 57.5506 Hz: 17376 us per frame */
#define GA_CPU_CLOCK 16000000u
#define GA_CYCLES_PER_FRAME 278016u           /* 16 MHz * 17376 us */
#define GA_YMZ_CLOCK 16000000u                /* gaia: 16 MHz (most Cave games use 16.9344 MHz) */
#define GA_AUDIO_RATE (GA_YMZ_CLOCK / 192.0)  /* 83,333.3 Hz */
#define GA_MAX_FRAME_SAMPLES 2048
#define GA_MAX_SPRITES 1024                   /* MAME MAX_SPRITE_NUM: one 0x4000-byte sprite RAM bank */

/* Inputs (active high here; the board ports are active low).  MAME
 * INPUT_PORTS_START(gaia): 8-way stick and four buttons per player. */
enum {
    GA_IN_P1_UP = 1u << 0, GA_IN_P1_DOWN = 1u << 1, GA_IN_P1_LEFT = 1u << 2, GA_IN_P1_RIGHT = 1u << 3,
    GA_IN_P1_B1 = 1u << 4, GA_IN_P1_B2 = 1u << 5, GA_IN_P1_B3 = 1u << 6, GA_IN_P1_B4 = 1u << 7,
    GA_IN_P2_UP = 1u << 8, GA_IN_P2_DOWN = 1u << 9, GA_IN_P2_LEFT = 1u << 10, GA_IN_P2_RIGHT = 1u << 11,
    GA_IN_P2_B1 = 1u << 12, GA_IN_P2_B2 = 1u << 13, GA_IN_P2_B3 = 1u << 14, GA_IN_P2_B4 = 1u << 15,
    GA_IN_COIN1 = 1u << 16, GA_IN_COIN2 = 1u << 17, GA_IN_TEST = 1u << 18, GA_IN_SERVICE1 = 1u << 19,
    GA_IN_P1_START = 1u << 20, GA_IN_P2_START = 1u << 21
};
#define GA_DSW_DEFAULT 0xFFFBu   /* MAME defaults: English, demo sounds on, 1C/1C, 3 lives, Medium */

/* One sprite as get_sprite_info_cave latches it at the VBLANK interrupt. */
typedef struct ga_sprite {
    int32_t priority, flags;
    uint32_t pen_data;           /* offset into the unpacked sprite pixels */
    int32_t line_offset;
    uint32_t base_pen;
    int32_t tile_width, tile_height, total_width, total_height;
    int32_t x, y, xcount0, ycount0, zoomx_re, zoomy_re;
} ga_sprite;

typedef struct ga_board {
    m68k_state cpu;

    /* ROM-derived, not part of the state */
    uint8_t *program;            /* 1 MiB 68000 image */
    uint8_t *sprite_gfx;         /* 8 MiB sprite ROM unpacked to 16M 4-bit pixels */
    uint32_t sprite_gfx_mask;
    uint8_t *layer_gfx[3];       /* 8x8x8 tiles decoded to 64 pixels each */
    uint32_t layer_tiles[3];
    uint8_t *samples;            /* 12 MiB YMZ280B sample ROM */
    char error[512];

    /* memory */
    uint8_t ram[0x10000];
    uint8_t spriteram[0x10000];
    uint8_t vram[3][0x10000];    /* 038 VRAM (16x16 map, line RAM, scratch, 8x8 map) + "more layer" RAM */
    uint16_t videoregs[0x40];
    uint16_t layer_vregs[3][3];
    uint8_t palette_ram[0x10000];
    uint32_t pens[0x8000];

    /* video state */
    uint8_t tiledim[3];          /* tmap038 m_tiledim, latched at screen update */
    int32_t spriteram_bank;
    int32_t num_sprites;
    ga_sprite sprites[GA_MAX_SPRITES];
    uint16_t zbuf[GA_SCREEN_TOTAL_H * GA_SCREEN_W];
    uint16_t zbuf_baseval;
    uint32_t screen[GA_SCREEN_H * GA_SCREEN_W];

    /* interrupts */
    uint8_t vblank_irq, sound_irq, unknown_irq, agallet_vblank_irq;

    ymz280b ymz;

    uint32_t inputs;             /* active-high GA_IN_* */
    uint16_t dsw;                /* active low, as the port reads */
    uint64_t lines_run;
    uint16_t vpos;
    uint16_t frame_end_line;
    uint64_t frames_run;
    uint64_t next_vblank_irq, next_vblank_end;   /* CPU cycles; UINT64_MAX = none */
    uint64_t next_sound_update;  /* MAME's 50 Hz sound-manager stream update (end-of-sample IRQs are noticed there) */
    uint64_t audio_samples;      /* YMZ280B samples generated since reset (one per 192 CPU cycles) */
} ga_board;

int  ga_rom_check(const char *zip_path, char *err, size_t n);
int  ga_board_load(ga_board *b, const char *zip_path);
void ga_board_free(ga_board *b);
void ga_board_reset(ga_board *b);
void ga_board_attach(ga_board *b);
void ga_board_set_inputs(ga_board *b, uint32_t active_high);

#define GA_RUN_OK 0
#define GA_RUN_MISSING 1
int ga_board_run_frame(ga_board *b, int16_t *stereo, int *frames);

/* video.c */
void ga_video_decode(ga_board *b, const uint8_t *sprite_rom, const uint8_t *const layer_rom[3]);
void ga_video_palette_write(ga_board *b, unsigned word);
void ga_video_latch_sprites(ga_board *b);        /* get_sprite_info, at the VBLANK interrupt */
void ga_video_update(ga_board *b);               /* screen_update, at VBLANK */
void ga_video_render(const ga_board *b, uint32_t *pixels);

#ifdef __cplusplus
}
#endif
#endif
