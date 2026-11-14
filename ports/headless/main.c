/* Deterministic headless runner for Gaia Crusaders: runs the translated
 * program on the board model and writes hashes, PNG, WAV and memory dumps. */
#include "ga/board.h"
#include "ga/state.h"
#include "m68k/runner.h"
#include "m68k/program_api.h"
#include "miniz.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fnv1a(uint64_t h, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}

static int write_png(const char *path, const uint32_t *px, int w, int h)
{
    /* tdefl_write_image_to_png_file_in_memory takes packed RGB rows and adds
     * the PNG filter bytes itself */
    uint8_t *r = (uint8_t *)malloc((size_t)w * (size_t)h * 3);
    if (!r) return 0;
    for (int i = 0; i < w * h; i++) {
        r[i * 3] = (uint8_t)(px[i] >> 16);
        r[i * 3 + 1] = (uint8_t)(px[i] >> 8);
        r[i * 3 + 2] = (uint8_t)px[i];
    }
    size_t zl = 0;
    void *z = tdefl_write_image_to_png_file_in_memory(r, w, h, 3, &zl);
    free(r);
    if (!z) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) { mz_free(z); return 0; }
    fwrite(z, 1, zl, f);
    fclose(f);
    mz_free(z);
    return 1;
}

int main(int argc, char **argv)
{
    const char *rom = NULL, *png = NULL, *dump_ram = NULL, *wav = NULL, *hashes = NULL, *script = NULL;
    int frame_end_line = 0;
    const char *dump_video = NULL;
    int seq_from = -1, seq_to = -1; const char *seq_file = NULL;
    int frames = 600, state_test = 0, state_test_len = 120, png_every = 0;
    long watch = -1;
    int dips = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom") && i + 1 < argc) rom = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--watch") && i + 1 < argc) watch = strtol(argv[++i], NULL, 16) & 0xFFFFF;
        else if (!strcmp(argv[i], "--strict")) m68k_set_strict(1);
        else if (!strcmp(argv[i], "--dips") && i + 1 < argc) dips = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--png") && i + 1 < argc) png = argv[++i];
        else if (!strcmp(argv[i], "--png-every") && i + 1 < argc) png_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-ram") && i + 1 < argc) dump_ram = argv[++i];
        else if (!strcmp(argv[i], "--dump-seq") && i + 3 < argc) { seq_from = atoi(argv[++i]); seq_to = atoi(argv[++i]); seq_file = argv[++i]; }
        else if (!strcmp(argv[i], "--dump-video") && i + 1 < argc) dump_video = argv[++i];
        else if (!strcmp(argv[i], "--frame-end-line") && i + 1 < argc) frame_end_line = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
        else if (!strcmp(argv[i], "--hashes") && i + 1 < argc) hashes = argv[++i];
        else if (!strcmp(argv[i], "--inputs") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--state-test") && i + 1 < argc) state_test = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--state-test-len") && i + 1 < argc) state_test_len = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: %s --rom gaia.zip [--frames N] [--png FILE] [--png-every N]\n"
                            "          [--dump-ram FILE] [--wav FILE] [--hashes FILE] [--inputs auto|random:N]\n"
                            "          [--state-test FRAME] [--state-test-len N] [--dips 0xNN]\n", argv[0]);
            return 2;
        }
    }
    if (!rom) rom = getenv("GAIA_ROM");
    if (!rom) { fprintf(stderr, "error: --rom is required\n"); return 2; }

    static ga_board board;
    m68k_program_init();
    if (!ga_board_load(&board, rom)) { fprintf(stderr, "error: %s\n", board.error); return 1; }
    if (dips >= 0) board.dsw = (uint16_t)dips;
    ga_board_reset(&board);
    if (frame_end_line > 0 && frame_end_line < GA_LINES) board.frame_end_line = (uint16_t)frame_end_line;
    printf("ROM: %s\nprogram: reset PC %06X, %u 68000 entries\n", rom, board.cpu.pc, m68k_program_entry_count());

    uint32_t *pixels = (uint32_t *)calloc(GA_SCREEN_W * GA_SCREEN_H, 4);
    static int16_t audio[GA_MAX_FRAME_SAMPLES * 2];
    FILE *wf = wav ? fopen(wav, "wb") : NULL;
    uint32_t wav_frames = 0;
    if (wf) { uint8_t h[44] = { 0 }; fwrite(h, 1, 44, wf); }
    FILE *sf = seq_file ? fopen(seq_file, "wb") : NULL;
    FILE *hf = hashes ? fopen(hashes, "w") : NULL;
    uint8_t *saved = NULL; size_t saved_size = 0;
    uint64_t *th = state_test ? (uint64_t *)calloc((size_t)state_test_len, 8) : NULL;
    int tphase = 0, tindex = 0, tmismatch = -1;
    int failed = 0;
    uint32_t rng = 1, held = 0, saved_rng = 0, saved_held = 0;   /* input script state (restored with the test state) */

    for (int frame = 0; frame < frames; frame++) {
        /* snapshot before this frame's inputs are generated, so a replay
         * regenerates them from the same script state */
        if (state_test && frame == state_test && tphase == 0) {
            saved = ga_state_save(&board, &saved_size);
            saved_rng = rng; saved_held = held;
            printf("state-test: saved %zu bytes at frame %d\n", saved_size, frame);
            tphase = 1; tindex = 0;
        }
        if (script && strcmp(script, "none") != 0) {
            uint32_t in = 0;
            if (!strncmp(script, "hold:", 5)) {
                in = (uint32_t)strtoul(script + 5, NULL, 0);
            } else if (!strncmp(script, "random:", 7)) {
                if (frame == 0) rng = (uint32_t)strtoul(script + 7, NULL, 0) * 2654435761u + 1u;
                int t = frame % 900;
                if (frame > 600 && t < 5) in |= GA_IN_COIN1;
                if (frame > 600 && t >= 450 && t < 455) in |= GA_IN_COIN2;
                if (frame > 600 && t >= 20 && t < 26) in |= GA_IN_P1_START;
                if (frame > 600 && t >= 470 && t < 476) in |= GA_IN_P2_START;
                if (frame % 6 == 0) { rng = rng * 1103515245u + 12345u; held = (rng >> 8) & 0x7F7Fu; }
                in |= held;
            } else {
                int t = frame % 600;
                if (frame > 900 && t < 5) in |= GA_IN_COIN1;
                if (frame > 900 && t >= 30 && t < 36) in |= GA_IN_P1_START;
                if (frame > 1000) {
                    in |= ((frame / 90) % 2) ? GA_IN_P1_RIGHT : GA_IN_P1_LEFT;
                    if (frame % 12 < 3) in |= GA_IN_P1_B1;
                    if (frame % 97 < 2) in |= GA_IN_P1_B2;
                }
            }
            ga_board_set_inputs(&board, in);
        }
        int n = 0;
        int rc = ga_board_run_frame(&board, audio, &n);
        if (wf) { fwrite(audio, 4, (size_t)n, wf); wav_frames += (uint32_t)n; }
        if (rc != GA_RUN_OK) {
            fprintf(stderr, "stopped at frame %d: 68000 reached untranslated code at %06X\n", frame, m68k_last_missing_pc());
            failed = 1;
            break;
        }
        if (tphase == 1 || tphase == 2) {
            uint64_t h = fnv1a(0xcbf29ce484222325ull, board.ram, sizeof(board.ram));
            h = fnv1a(h, audio, (size_t)n * 4);
            h = fnv1a(h, board.screen, sizeof(board.screen));
            h = fnv1a(h, board.spriteram, sizeof(board.spriteram));
            if (tphase == 1) th[tindex] = h; else if (th[tindex] != h && tmismatch < 0) tmismatch = tindex;
            if (++tindex == state_test_len) {
                if (tphase == 1) {
                    char err[200];
                    uint8_t *bad = (uint8_t *)malloc(saved_size);
                    memcpy(bad, saved, saved_size);
                    bad[saved_size / 2] ^= 0x5A;
                    uint32_t pc = board.cpu.pc;
                    int rej = !ga_state_load(&board, bad, saved_size, err, sizeof(err));
                    printf("state-test: corrupted state %s (%s), board %s\n", rej ? "rejected" : "ACCEPTED", err,
                           board.cpu.pc == pc ? "untouched" : "MODIFIED");
                    rej = !ga_state_load(&board, bad, saved_size / 3, err, sizeof(err));
                    printf("state-test: truncated state %s (%s)\n", rej ? "rejected" : "ACCEPTED", err);
                    free(bad);
                    if (!ga_state_load(&board, saved, saved_size, err, sizeof(err))) { printf("state-test: load FAILED: %s\n", err); failed = 1; break; }
                    rng = saved_rng; held = saved_held;
                    { size_t rs = 0; uint8_t *re = ga_state_save(&board, &rs);
                      size_t first = (size_t)-1; for (size_t q = 0; re && q < rs && q < saved_size; q++) if (re[q] != saved[q]) { first = q; break; }
                      printf("state-test: re-save %s (%zu bytes, first difference at %lld)\n", re && rs == saved_size && first == (size_t)-1 ? "identical" : "DIFFERS", rs, first == (size_t)-1 ? -1LL : (long long)first);
                      free(re); }
                    printf("state-test: restored, replaying %d frames\n", state_test_len);
                    tphase = 2; tindex = 0; frame = state_test - 1;
                    continue;
                }
                if (tmismatch < 0) printf("state-test: PASS, %d frames identical after load\n", state_test_len);
                else { printf("state-test: FAIL at +%d\n", tmismatch); failed = 1; }
                tphase = 3;
            }
        }
        if (watch >= 0) {
            printf("W %d", frame + 1);
            for (int k = 0; k < 5; k++) { const uint8_t *q = &board.ram[(watch + k * 4) & 0xFFFFC]; printf(" %u", (unsigned)(q[0] << 24 | q[1] << 16 | q[2] << 8 | q[3])); }
            printf("\n");
        }
        if (sf && frame + 1 >= seq_from && frame + 1 <= seq_to) fwrite(board.ram, 1, sizeof(board.ram), sf);
        uint64_t hr = fnv1a(0xcbf29ce484222325ull, board.ram, sizeof(board.ram));
        if (hf) fprintf(hf, "%d %06X %016" PRIx64 " %08X %08X\n", frame + 1, board.cpu.pc, hr, board.cpu.d[0], board.cpu.a[7]);
        if (frame % 300 == 0) printf("frame %5d pc %06X ram %016" PRIx64 "\n", frame, board.cpu.pc, hr);
        if (png_every && png && frame % png_every == png_every - 1) {
            char p[1024];
            snprintf(p, sizeof(p), "%s_%05d.png", png, frame + 1);
            ga_video_render(&board, pixels);
            write_png(p, pixels, GA_SCREEN_W, GA_SCREEN_H);
        }
    }
    if (!failed && png && !png_every) { ga_video_render(&board, pixels); write_png(png, pixels, GA_SCREEN_W, GA_SCREEN_H); }
    if (dump_video) {   /* raw video RAM blocks, big-endian as the bus sees them */
        FILE *f = fopen(dump_video, "wb");
        if (f) {
            fwrite(board.palette_ram, 1, sizeof(board.palette_ram), f);
            fwrite(board.vram, 1, sizeof(board.vram), f);
            fwrite(board.spriteram, 1, sizeof(board.spriteram), f);
            fwrite(board.videoregs, 1, sizeof(board.videoregs), f);
            fwrite(board.layer_vregs, 1, sizeof(board.layer_vregs), f);
            fclose(f);
        }
    }
    if (dump_ram) { FILE *f = fopen(dump_ram, "wb"); if (f) { fwrite(board.ram, 1, sizeof(board.ram), f); fclose(f); } }
    if (wf) {
        uint32_t rate = (uint32_t)(GA_AUDIO_RATE + 0.5), data = wav_frames * 4u, riff = 36 + data, br = rate * 4;
        uint8_t h[44] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 2,0 };
        memcpy(h + 4, &riff, 4); memcpy(h + 24, &rate, 4); memcpy(h + 28, &br, 4);
        h[32] = 4; h[34] = 16; memcpy(h + 36, "data", 4); memcpy(h + 40, &data, 4);
        fseek(wf, 0, SEEK_SET); fwrite(h, 1, 44, wf); fclose(wf);
    }
    if (hf) fclose(hf);
    if (sf) fclose(sf);
    {
        const uint32_t *pcs; unsigned np = m68k_fallback_entries(&pcs);
        printf("fallback: %" PRIu64 " interpreted instructions, %u entry points", m68k_fallback_instructions(), np);
        for (unsigned i = 0; i < np; i++) printf(" %06X", pcs[i]);
        printf("\n");
    }
    printf("CPU: %" PRIu64 " 68000 instructions, %" PRIu64 " cycles\n", board.cpu.instructions, board.cpu.cycles);
    free(saved); free(th); free(pixels);
    ga_board_free(&board);
    return failed;
}
