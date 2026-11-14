/* Gaia Crusaders - native port, SDL2 frontend adapter.
 *
 * Connects the Cave 68000 board runtime (runtime/) and the statically
 * translated 68000 program (build/generated/, or the fallback interpreter in
 * a generic build) to the shared frontend (frontend/).  The ROM set is read
 * from the player's archive at run time; no ROM data is built in. */
#include "fe.h"
#include "ga/board.h"
#include "ga/state.h"
#include "m68k/runner.h"
#include "m68k/program_api.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GA_VERSION
#define GA_VERSION "0.1.0"
#endif

static ga_board *board;
static int loaded;
static uint16_t dsw = GA_DSW_DEFAULT;

#define K(x) SDL_SCANCODE_##x
#define P(x) SDL_CONTROLLER_BUTTON_##x
/* MAME atlus/cave.cpp INPUT_PORTS_START(gaia): two players, 8-way stick and
 * four buttons each (MAME names them Button 1-4), start, two coin slots,
 * service coin and the test switch. */
static const fe_input_def INPUTS[] = {
    { "p1_up", "P1 Up", 1, GA_IN_P1_UP, K(UP), K(W), P(DPAD_UP), 0 },
    { "p1_down", "P1 Down", 1, GA_IN_P1_DOWN, K(DOWN), K(S), P(DPAD_DOWN), 0 },
    { "p1_left", "P1 Left", 1, GA_IN_P1_LEFT, K(LEFT), K(A), P(DPAD_LEFT), 0 },
    { "p1_right", "P1 Right", 1, GA_IN_P1_RIGHT, K(RIGHT), K(D), P(DPAD_RIGHT), 0 },
    { "p1_b1", "P1 Button 1", 1, GA_IN_P1_B1, K(Z), K(J), P(A), 0 },
    { "p1_b2", "P1 Button 2", 1, GA_IN_P1_B2, K(X), K(K), P(B), 0 },
    { "p1_b3", "P1 Button 3", 1, GA_IN_P1_B3, K(C), K(L), P(X), 0 },
    { "p1_b4", "P1 Button 4", 1, GA_IN_P1_B4, K(V), K(SEMICOLON), P(Y), 0 },
    { "p1_start", "P1 Start", 1, GA_IN_P1_START, K(RETURN), K(1), P(START), 0 },
    { "p2_up", "P2 Up", 2, GA_IN_P2_UP, K(I), -1, P(DPAD_UP), 0 },
    { "p2_down", "P2 Down", 2, GA_IN_P2_DOWN, K(COMMA), -1, P(DPAD_DOWN), 0 },
    { "p2_left", "P2 Left", 2, GA_IN_P2_LEFT, K(U), -1, P(DPAD_LEFT), 0 },
    { "p2_right", "P2 Right", 2, GA_IN_P2_RIGHT, K(O), -1, P(DPAD_RIGHT), 0 },
    { "p2_b1", "P2 Button 1", 2, GA_IN_P2_B1, K(KP_1), -1, P(A), 0 },
    { "p2_b2", "P2 Button 2", 2, GA_IN_P2_B2, K(KP_2), -1, P(B), 0 },
    { "p2_b3", "P2 Button 3", 2, GA_IN_P2_B3, K(KP_3), -1, P(X), 0 },
    { "p2_b4", "P2 Button 4", 2, GA_IN_P2_B4, K(KP_4), -1, P(Y), 0 },
    { "p2_start", "P2 Start", 2, GA_IN_P2_START, K(2), -1, P(START), 0 },
    /* coins are pulses, like a real coin switch (MAME PORT_IMPULSE(6)) */
    { "coin1", "Coin 1", 1, GA_IN_COIN1, K(5), -1, P(BACK), 6 },
    { "coin2", "Coin 2", 2, GA_IN_COIN2, K(6), -1, P(BACK), 6 },
    { "service1", "Service Coin", 0, GA_IN_SERVICE1, K(9), -1, -1, 6 },
    { "test", "Test / Service Mode", 0, GA_IN_TEST, K(F2), -1, -1, 0 },
};

/* MAME PORT_DIPNAMEs of INPUT_PORTS_START(gaia), active low.  Gaia has no
 * EEPROM: the cabinet settings are these switches. */
static const fe_dip_def DIPS[] = {
    { "flip_screen", "Flip Screen", 0, 0x0001, 0x0001, 2, { { "Off", 0x0001 }, { "On", 0x0000 } } },
    { "demo_sounds", "Demo Sounds", 0, 0x0002, 0x0002, 2, { { "Off", 0x0000 }, { "On", 0x0002 } } },
    { "language", "Language", 0, 0x0004, 0x0000, 2, { { "English", 0x0000 }, { "Japanese", 0x0004 } } },
    { "coinage", "Coinage", 0, 0x0038, 0x0038, 8, {
        { "4 Coins / 1 Credit", 0x0008 }, { "3 Coins / 1 Credit", 0x0010 }, { "2 Coins / 1 Credit", 0x0020 },
        { "2 Coins / 1 Credit (1 to continue)", 0x0000 }, { "1 Coin / 1 Credit", 0x0038 },
        { "2 Coins / 3 Credits", 0x0018 }, { "1 Coin / 2 Credits", 0x0030 }, { "1 Coin / 3 Credits", 0x0028 } } },
    { "free_play", "Free Play", 0, 0x0040, 0x0040, 2, { { "Off", 0x0040 }, { "On", 0x0000 } } },
    { "continue", "Allow Continue", 0, 0x0080, 0x0080, 2, { { "Off", 0x0000 }, { "On", 0x0080 } } },
    { "lives", "Lives", 0, 0x0300, 0x0300, 4, { { "1", 0x0100 }, { "2", 0x0000 }, { "3", 0x0300 }, { "4", 0x0200 } } },
    { "bonus_life", "Bonus Life", 0, 0x0400, 0x0400, 2, { { "Off", 0x0000 }, { "On (thresholds depend on Difficulty)", 0x0400 } } },
    { "damage", "Damage", 0, 0x1800, 0x1800, 4, { { "+0", 0x1800 }, { "+1", 0x1000 }, { "+2", 0x0800 }, { "+3", 0x0000 } } },
    { "difficulty", "Difficulty", 0, 0xE000, 0xE000, 8, {
        { "Very Easy", 0xC000 }, { "Easy", 0xA000 }, { "Medium", 0xE000 }, { "Medium Hard", 0x6000 },
        { "Hard 1", 0x8000 }, { "Hard 2", 0x2000 }, { "Very Hard", 0x4000 }, { "Hardest", 0x0000 } } },
};

/* Scores: 32-bit values at 0x1058C2 (P1) and 0x1059C2 (P2).  Matched against
 * the HUD at frames 2400, 2700, 3000 and 3300 of the attract demo (2300/600,
 * 5000/600, 7900/1100, 11200/1550), where work RAM is byte-identical to MAME
 * 0.264; no other address in work RAM holds those values at all four frames. */
static uint32_t ram32(uint32_t address)
{
    const uint8_t *p = &board->ram[address - 0x100000];
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static int gadget_score1(char *t, size_t n) { if (!loaded) return 0; snprintf(t, n, "1P %7u", ram32(0x1058C2)); return 1; }
static int gadget_score2(char *t, size_t n) { if (!loaded) return 0; snprintf(t, n, "2P %7u", ram32(0x1059C2)); return 1; }

static const fe_gadget_def GADGETS[] = {
    { "score_p1", "Score (Player 1)", gadget_score1 },
    { "score_p2", "Score (Player 2)", gadget_score2 },
};

static int g_validate(const char *path, char *err, size_t n) { return ga_rom_check(path, err, n); }

static int g_load(const char *path, char *err, size_t n)
{
    if (!board) board = (ga_board *)calloc(1, sizeof(ga_board));
    if (!board) { snprintf(err, n, "out of memory"); return 0; }
    if (loaded) { ga_board_free(board); loaded = 0; }
    m68k_program_init();
    if (!ga_board_load(board, path)) { snprintf(err, n, "%s", board->error); return 0; }
    board->dsw = dsw;
    ga_board_reset(board);
    loaded = 1;
    return 1;
}

static void g_unload(void) { if (loaded) ga_board_free(board); loaded = 0; }
static void g_reset(void) { board->dsw = dsw; ga_board_reset(board); }
static void g_set_dips(const uint16_t *w) { dsw = w[0]; if (loaded) board->dsw = dsw; }
static void g_set_inputs(uint32_t m) { ga_board_set_inputs(board, m); }

static int g_run(int16_t *audio, int *frames, char *err, size_t n)
{
    if (ga_board_run_frame(board, audio, frames) != GA_RUN_OK) {
        snprintf(err, n, "The 68000 program stopped at address %06X (an instruction the runtime cannot execute).",
                 m68k_last_missing_pc());
        return 1;
    }
    return 0;
}

static void g_render32(uint32_t *px) { ga_video_render(board, px); }
static uint8_t *g_save(size_t *size) { return ga_state_save(board, size); }
static int g_validate_state(const uint8_t *d, size_t s, char *e, size_t n) { return ga_state_validate(board, d, s, e, n); }
static int g_load_state(const uint8_t *d, size_t s, char *e, size_t n) { return ga_state_load(board, d, s, e, n); }
static uint32_t g_fingerprint(void) { return loaded ? ga_board_rom_fingerprint(board) : 0; }

static const fe_game GAME = {
    .id = "gaia",
    .title = "Gaia Crusaders",
    .subtitle = "native port",
    .about =
        "Gaia Crusaders  (Noise Factory, 1999, Cave 68000 hardware)\n"
        "Native port " GA_VERSION ": the 68000 game program is translated ahead of time to C;\n"
        "code the static analysis did not reach runs on a fallback 68000 interpreter built from\n"
        "the same decoder, timing and instruction helpers. The board (three 038 tilemaps, the\n"
        "013 sprite generator, palette, YMZ280B, interrupts) is reimplemented natively.\n"
        "\n"
        "Requires your own MAME 'gaia' ROM set (10 files). No game data is distributed.\n"
        "Hardware behaviour references MAME (BSD-3-Clause). 68000 decoder: arcade-decomp (BSD-3-Clause).\n"
        "Frontend font: Inter (SIL Open Font License 1.1). SDL2 (zlib). miniz (MIT).\n"
        "This project's own code is GPL-2.0. See the LICENSES folder.",
    .rom_hint = "gaia.zip",
    .width = GA_SCREEN_W,
    .height = GA_SCREEN_H,
    .display_aspect = 4.0 / 3.0,
    .rotation = 0,
    .refresh_hz = GA_REFRESH,
    .audio_rate = GA_AUDIO_RATE,
    .max_audio_frames = GA_MAX_FRAME_SAMPLES,
    .dip_words = 1,
    .players = 2,
    .inputs = INPUTS, .input_count = (int)(sizeof(INPUTS) / sizeof(INPUTS[0])),
    .dips = DIPS, .dip_count = (int)(sizeof(DIPS) / sizeof(DIPS[0])),
    .gadgets = GADGETS, .gadget_count = (int)(sizeof(GADGETS) / sizeof(GADGETS[0])),
    .validate_rom = g_validate,
    .load = g_load,
    .unload = g_unload,
    .reset = g_reset,
    .set_dips = g_set_dips,
    .set_inputs = g_set_inputs,
    .run_frame = g_run,
    .render = NULL,
    .render32 = g_render32,
    .save_state = g_save,
    .validate_state = g_validate_state,
    .load_state = g_load_state,
    .rom_fingerprint = g_fingerprint,
    .runtime_version = GA_VERSION,
    .nvram_save = NULL,
    .nvram_load = NULL,
    .nvram_dirty = NULL,
    .profile_name = "gaia",
};

int main(int argc, char **argv)
{
    int rc = fe_run(&GAME, argc, argv);
    if (loaded) ga_board_free(board);
    free(board);
    return rc;
}
