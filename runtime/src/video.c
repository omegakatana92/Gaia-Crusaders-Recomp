/* Cave video for Gaia Crusaders: three 038 tilemaps (8x8x8 tiles, 8x8 or
 * 16x16 maps, row scroll / row select), the 013 sprite generator with zoom
 * (4-bpp sprites with 256-colour granularity, as MAME's gaia config uses)
 * and the xGRB_555 palette.  After MAME 0.264 src/mame/atlus/cave_v.cpp
 * (get_sprite_info_cave, sprite_check, do_blit_*, tilemap_draw,
 * screen_update), src/devices/video/tmap038.cpp and the tilemap drawing
 * rules of src/emu/tilemap.cpp; BSD-3-Clause, Luca Elia, David Haywood et al.
 * Tile and sprite decoding follow the gfx layouts in cave.cpp (the decoders
 * started from the ESP Ra.De. port). */
#include "ga/board.h"

#include <stdlib.h>
#include <string.h>

#define FLIPX 0x01
#define FLIPY 0x02
#define CLIP_L 0
#define CLIP_R GA_SCREEN_W
#define CLIP_T 0
#define CLIP_B GA_SCREEN_H

static inline uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline int bit_at(const uint8_t *d, int bit) { return (d[bit >> 3] >> (7 - (bit & 7))) & 1; }

/* ------------------------------------------------------------- decoding */

/* layout_8x8x8: planes {8,9,10,11,0,1,2,3}, x {0,4,16,20,32,36,48,52}, y step 64 bits */
static void decode_tiles(const uint8_t *rom, uint32_t tiles, uint8_t *out)
{
    static const int plane[8] = { 8, 9, 10, 11, 0, 1, 2, 3 };
    static const int xo[8] = { 0, 4, 16, 20, 32, 36, 48, 52 };
    for (uint32_t t = 0; t < tiles; t++) {
        const uint8_t *src = rom + (size_t)t * 64;
        uint8_t *dst = out + (size_t)t * 64;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int pen = 0;
                for (int p = 0; p < 8; p++) pen |= bit_at(src, plane[p] + xo[x] + y * 64) << (7 - p);
                dst[y * 8 + x] = (uint8_t)pen;
            }
    }
}

/* layout_sprites: 16x1, 4 planes STEP4(0,1), x {4,0,12,8,...}: the low
 * nibble of each byte is the left pixel */
static void decode_sprites(const uint8_t *rom, uint32_t bytes, uint8_t *out)
{
    for (uint32_t i = 0; i < bytes; i++) {
        out[i * 2] = rom[i] & 0x0F;
        out[i * 2 + 1] = rom[i] >> 4;
    }
}

void ga_video_decode(ga_board *b, const uint8_t *sprite_rom, const uint8_t *const layer_rom[3])
{
    b->sprite_gfx = (uint8_t *)malloc(0x1000000);
    b->sprite_gfx_mask = 0x1000000 - 1;
    if (b->sprite_gfx) decode_sprites(sprite_rom, 0x800000, b->sprite_gfx);
    for (int l = 0; l < 3; l++) {
        b->layer_tiles[l] = 0x400000 / 64;
        b->layer_gfx[l] = (uint8_t *)malloc(0x400000);
        if (b->layer_gfx[l]) decode_tiles(layer_rom[l], b->layer_tiles[l], b->layer_gfx[l]);
    }
}

/* ---------------------------------------------------------------- palette */

static inline uint32_t pal5(uint32_t v) { return (v << 3) | (v >> 2); }

void ga_video_palette_write(ga_board *b, unsigned word)
{
    uint16_t v = be16(&b->palette_ram[word * 2]);   /* xGRB_555 */
    b->pens[word] = pal5((v >> 5) & 31) << 16 | pal5((v >> 10) & 31) << 8 | pal5(v & 31);
}

/* ---------------------------------------------------------------- sprites */

/* get_sprite_info_cave, run at the VBLANK interrupt (cave_state::vblank_start) */
void ga_video_latch_sprites(ga_board *b)
{
    b->spriteram_bank = (b->videoregs[4] & 3) % 4;
    const uint8_t *src = &b->spriteram[0x4000 * b->spriteram_bank];
    const int glob_flipx = b->videoregs[0] & 0x8000, glob_flipy = b->videoregs[1] & 0x8000;
    const int max_x = GA_SCREEN_W, max_y = GA_SCREEN_TOTAL_H;
    const uint32_t max_clk = 448u * 272u * 2u;
    uint32_t clk = 0;
    int n = 0;
    for (int i = 0; i < 0x4000 / 16; i++, src += 16) {
        clk += 32;
        if (clk > max_clk) break;
        int x, y;
        if ((b->videoregs[5] & 0x3000) == 0) { x = (be16(src) & 0x3ff) << 8; y = (be16(src + 2) & 0x3ff) << 8; }
        else { x = be16(src) << 2; y = be16(src + 2) << 2; }
        uint16_t attr = be16(src + 4);
        uint32_t code = be16(src + 6) + ((uint32_t)(attr & 3) << 16);
        int zoomx = be16(src + 8), zoomy = be16(src + 10);
        uint16_t size = be16(src + 12);
        ga_sprite *s = &b->sprites[n];
        s->tile_width = ((size >> 8) & 0x1f) * 16;
        s->tile_height = (size & 0x1f) * 16;
        if (!s->tile_width || !s->tile_height) continue;
        clk += (uint32_t)(s->tile_width * s->tile_height);
        if (clk > max_clk) break;
        s->pen_data = (code << 8) & b->sprite_gfx_mask;
        int flipx = attr & 0x0008, flipy = attr & 0x0004;
        int total_width_f, total_height_f;
        s->total_width = (total_width_f = s->tile_width * zoomx) / 0x100;
        s->total_height = (total_height_f = s->tile_height * zoomy) / 0x100;
        if (s->total_width <= 1) {
            s->total_width = 1; s->zoomx_re = s->tile_width << 16; s->xcount0 = s->zoomx_re / 2; x -= 0x80;
        } else {
            s->zoomx_re = zoomx ? 0x1000000 / zoomx : 0; s->xcount0 = s->zoomx_re - 1;
        }
        if (s->total_height <= 1) {
            s->total_height = 1; s->zoomy_re = s->tile_height << 16; s->ycount0 = s->zoomy_re / 2; y -= 0x80;
        } else {
            s->zoomy_re = zoomy ? 0x1000000 / zoomy : 0; s->ycount0 = s->zoomy_re - 1;
        }
        if ((b->videoregs[5] & 0x3000) == 0) {
            x >>= 8; y >>= 8;
            if (flipx && zoomx != 0x100) x += s->tile_width - s->total_width;
            if (flipy && zoomy != 0x100) y += s->tile_height - s->total_height;
        } else {
            if (flipx && zoomx != 0x100) x += (s->tile_width << 8) - total_width_f - 0x80;
            if (flipy && zoomy != 0x100) y += (s->tile_height << 8) - total_height_f - 0x80;
            x >>= 8; y >>= 8;
        }
        if (x > 0x1ff) x -= 0x400;
        if (y > 0x1ff) y -= 0x400;
        if (x + s->total_width <= 0 || x >= max_x || y + s->total_height <= 0 || y >= max_y) continue;
        s->priority = (attr & 0x0030) >> 4;
        s->flags = 0;
        s->line_offset = s->tile_width;
        s->base_pen = ((attr & 0x3f00) >> 8) * 256u;   /* granularity 256 (spr_8bpp video start) */
        if (glob_flipx) { x = max_x - x - s->total_width; flipx = !flipx; }
        if (glob_flipy) { y = max_y - y - s->total_height; flipy = !flipy; }
        s->x = x; s->y = y;
        if (flipx) s->flags |= FLIPX;
        if (flipy) s->flags |= FLIPY;
        n++;
    }
    b->num_sprites = n;
}

/* do_blit_zoom32 / do_blit_zoom32_zb */
static void blit_zoom(ga_board *b, const ga_sprite *s, int zb, uint16_t pri_sp)
{
    int x1, x2, y1, y2, dx, dy;
    int xcount0 = 0x10000 + s->xcount0, ycount0 = 0x10000 + s->ycount0;
    if (s->flags & FLIPX) {
        x2 = s->x; x1 = x2 + s->total_width; dx = -1;
        if (x2 < CLIP_L) x2 = CLIP_L;
        if (x1 > CLIP_R) {
            xcount0 += (x1 - CLIP_R) * s->zoomx_re; x1 = CLIP_R;
            while ((xcount0 & 0xffff) >= s->zoomx_re) { xcount0 += s->zoomx_re; x1--; }
        }
        if (x2 >= x1) return;
        x1--; x2--;
    } else {
        x1 = s->x; x2 = x1 + s->total_width; dx = 1;
        if (x1 < CLIP_L) {
            xcount0 += (CLIP_L - x1) * s->zoomx_re; x1 = CLIP_L;
            while ((xcount0 & 0xffff) >= s->zoomx_re) { xcount0 += s->zoomx_re; x1++; }
        }
        if (x2 > CLIP_R) x2 = CLIP_R;
        if (x1 >= x2) return;
    }
    if (s->flags & FLIPY) {
        y2 = s->y; y1 = y2 + s->total_height; dy = -1;
        if (y2 < CLIP_T) y2 = CLIP_T;
        if (y1 > CLIP_B) {
            ycount0 += (y1 - CLIP_B) * s->zoomy_re; y1 = CLIP_B;
            while ((ycount0 & 0xffff) >= s->zoomy_re) { ycount0 += s->zoomy_re; y1--; }
        }
        if (y2 >= y1) return;
        y1--; y2--;
    } else {
        y1 = s->y; y2 = y1 + s->total_height; dy = 1;
        if (y1 < CLIP_T) {
            ycount0 += (CLIP_T - y1) * s->zoomy_re; y1 = CLIP_T;
            while ((ycount0 & 0xffff) >= s->zoomy_re) { ycount0 += s->zoomy_re; y1++; }
        }
        if (y2 > CLIP_B) y2 = CLIP_B;
        if (y1 >= y2) return;
    }
    /* pen_data starts one pixel and one line before the sprite: the counters
     * step first, then read */
    int64_t pen_off = (int64_t)s->pen_data - 1 - s->line_offset;
    int ycount = ycount0;
    for (int y = y1; y != y2; y += dy) {
        if (ycount & 0xffff0000) {
            int xcount = xcount0;
            pen_off += (int64_t)s->line_offset * (ycount >> 16);
            ycount &= 0xffff;
            int64_t src = pen_off;
            uint32_t *dest = &b->screen[y * GA_SCREEN_W];
            uint16_t *z = &b->zbuf[y * GA_SCREEN_W];
            for (int x = x1; x != x2; x += dx) {
                if (xcount & 0xffff0000) {
                    src += xcount >> 16;
                    xcount &= 0xffff;
                    uint8_t pen = b->sprite_gfx[(uint32_t)src & b->sprite_gfx_mask];
                    if (pen && (!zb || z[x] <= pri_sp)) {
                        dest[x] = b->pens[(s->base_pen + pen) & 0x7fff];
                        if (zb) z[x] = pri_sp;
                    }
                }
                xcount += s->zoomx_re;
            }
        }
        ycount += s->zoomy_re;
    }
}

/* do_blit_32 / do_blit_32_zb */
static void blit(ga_board *b, const ga_sprite *s, int zb, uint16_t pri_sp)
{
    int x1, x2, y1, y2, dx, dy, xcount0 = 0, ycount0 = 0;
    if (s->flags & FLIPX) {
        x2 = s->x; x1 = x2 + s->total_width; dx = -1;
        if (x2 < CLIP_L) x2 = CLIP_L;
        if (x1 > CLIP_R) { xcount0 = x1 - CLIP_R; x1 = CLIP_R; }
        if (x2 >= x1) return;
        x1--; x2--;
    } else {
        x1 = s->x; x2 = x1 + s->total_width; dx = 1;
        if (x1 < CLIP_L) { xcount0 = CLIP_L - x1; x1 = CLIP_L; }
        if (x2 > CLIP_R) x2 = CLIP_R;
        if (x1 >= x2) return;
    }
    if (s->flags & FLIPY) {
        y2 = s->y; y1 = y2 + s->total_height; dy = -1;
        if (y2 < CLIP_T) y2 = CLIP_T;
        if (y1 > CLIP_B) { ycount0 = y1 - CLIP_B; y1 = CLIP_B; }
        if (y2 >= y1) return;
        y1--; y2--;
    } else {
        y1 = s->y; y2 = y1 + s->total_height; dy = 1;
        if (y1 < CLIP_T) { ycount0 = CLIP_T - y1; y1 = CLIP_T; }
        if (y2 > CLIP_B) y2 = CLIP_B;
        if (y1 >= y2) return;
    }
    uint32_t pen_off = s->pen_data + (uint32_t)(s->line_offset * ycount0 + xcount0);
    for (int y = y1; y != y2; y += dy) {
        uint32_t src = pen_off;
        uint32_t *dest = &b->screen[y * GA_SCREEN_W];
        uint16_t *z = &b->zbuf[y * GA_SCREEN_W];
        for (int x = x1; x != x2; x += dx) {
            uint8_t pen = b->sprite_gfx[src & b->sprite_gfx_mask];
            if (pen && (!zb || z[x] <= pri_sp)) {
                dest[x] = b->pens[(s->base_pen + pen) & 0x7fff];
                if (zb) z[x] = pri_sp;
            }
            src++;
        }
        pen_off += (uint32_t)s->line_offset;
    }
}

/* ---------------------------------------------------------------- tilemaps */

typedef struct layer_view {
    int on, flipx, flipy, sx, sy, offs_row, rowscroll, rowselect, tiledim;
} layer_view;

static inline uint16_t lineram(const ga_board *b, int layer, int line, int which)
{
    return be16(&b->vram[layer][0x1000 + ((line & 0x1ff) * 2 + which) * 2]);
}

/* tilemap038 get_tile_info for logical 8x8 cell (col, row) */
static inline void tile_info(const ga_board *b, int layer, int tiledim, int col, int row,
                             uint32_t *code, uint32_t *color, uint32_t *pri)
{
    const uint8_t *v = b->vram[layer];
    uint32_t tile;
    int tile_index = row * 64 + col;
    if (tiledim) {
        int t = (tile_index % 64) / 2 + ((tile_index / 64) / 2) * 32;
        tile = (uint32_t)be16(&v[t * 4]) << 16 | be16(&v[t * 4 + 2]);
        *code = (tile & 0xffff) * 4;
        *code ^= (uint32_t)(tile_index & 1);
        *code ^= (uint32_t)(((tile_index / 64) & 1) * 2);
    } else {
        tile = (uint32_t)be16(&v[0x4000 + tile_index * 4]) << 16 | be16(&v[0x4000 + tile_index * 4 + 2]);
        *code = tile & 0x3ffff;
    }
    *color = (tile & 0x3f000000) >> 24;
    *pri = (tile & 0xc0000000) >> 30;
}

static void setup_layer(const ga_board *b, int layer, layer_view *v)
{
    const uint16_t *r = b->layer_vregs[layer];
    v->on = !(r[2] & 0x10);
    v->flipx = !(r[0] & 0x8000);
    v->flipy = !(r[1] & 0x8000);
    v->tiledim = b->tiledim[layer];
    int offs_x = 0x13 - (v->tiledim ? (layer + 1) : (layer + 1 + 8));
    int offs_y = -0x12;
    v->offs_row = v->flipy ? 1 : -1;
    v->sx = (r[0] & 0x1ff) - b->videoregs[0] + (v->flipx ? (offs_x + 2) : -offs_x);
    v->sy = (r[1] & 0x1ff) - b->videoregs[1] + (v->flipy ? (offs_y + 2) : -offs_y);
    v->rowscroll = (r[0] & 0x4000) != 0;
    v->rowselect = (r[1] & 0x4000) != 0;
}

/* tilemap_draw for one layer and tile category 'pri'.  Screen pixel (x, y)
 * shows logical pixel (((flipx ? W-1-x : x) + scroll) & 511, row) where for
 * line index l (= y, or H-1-y when flipped) row = rowselect ? lineram(sy+offs_row+l)
 * : sy + l and scroll = sx (+ rowscroll(sy+offs_row+l)); the logical image is
 * mirrored as a whole when flipped, as MAME's tilemap flip does. */
static void draw_layer(ga_board *b, int layer, const layer_view *v, uint32_t pri)
{
    const uint8_t *gfx = b->layer_gfx[layer];
    const uint32_t tiles = b->layer_tiles[layer];
    for (int y = 0; y < GA_SCREEN_H; y++) {
        int l = v->flipy ? GA_SCREEN_H - 1 - y : y;
        int row = v->rowselect ? lineram(b, layer, v->sy + v->offs_row + l, 1) : v->sy + l;
        int scroll = v->sx + (v->rowscroll ? (int16_t)lineram(b, layer, v->sy + v->offs_row + l, 0) : 0);
        row &= 511;
        uint32_t *dest = &b->screen[y * GA_SCREEN_W];
        int cached_col = -1;
        uint32_t code = 0, color = 0, tpri = 0;
        for (int x = 0; x < GA_SCREEN_W; x++) {
            int m = v->flipx ? GA_SCREEN_W - 1 - x : x;
            int col = (m + scroll) & 511;
            if ((col >> 3) != cached_col) {
                cached_col = col >> 3;
                tile_info(b, layer, v->tiledim, cached_col, row >> 3, &code, &color, &tpri);
                code %= tiles;
            }
            if (tpri != pri) continue;
            uint8_t pen = gfx[(size_t)code * 64 + (row & 7) * 8 + (col & 7)];
            if (pen) dest[x] = b->pens[(0x4000 + color * 256 + pen) & 0x7fff];
        }
    }
}

/* ----------------------------------------------------------- screen update */

void ga_video_update(ga_board *b)
{
    /* tilemap038::prepare: the 8x8 / 16x16 choice is latched here */
    for (int l = 0; l < 3; l++) b->tiledim[l] = (b->layer_vregs[l][1] >> 13) & 1;

    /* sprite_check: per-priority lists in RAM order; a priority that goes
     * down along the list switches to the Z-buffered blitters */
    static const ga_sprite *table[4][GA_MAX_SPRITES + 1];
    int cnt[4] = { 0, 0, 0, 0 }, zbuf = 0, priority_check = 0;
    for (int i = 0; i < b->num_sprites; i++) {
        const ga_sprite *s = &b->sprites[i];
        if (s->x + s->total_width > CLIP_L && s->x < CLIP_R && s->y + s->total_height > CLIP_T && s->y < CLIP_B) {
            table[s->priority][cnt[s->priority]++] = s;
            if (!zbuf) {
                if (priority_check > s->priority) zbuf = 1;
                else priority_check = s->priority;
            }
        }
    }
    if (zbuf) {
        b->zbuf_baseval = (uint16_t)(b->zbuf_baseval + GA_MAX_SPRITES);
        if (!b->zbuf_baseval) memset(b->zbuf, 0, sizeof(uint16_t) * GA_SCREEN_W * GA_SCREEN_H);
    }

    /* background pen: colorbase 0x4000 + (64 - 1) * 256 */
    uint32_t bg = b->pens[0x7F00];
    for (int i = 0; i < GA_SCREEN_W * GA_SCREEN_H; i++) b->screen[i] = bg;

    layer_view v[3];
    for (int l = 0; l < 3; l++) setup_layer(b, l, &v[l]);

    for (int pri = 0; pri <= 3; pri++) {
        for (int i = 0; i < cnt[pri]; i++) {
            const ga_sprite *s = table[pri][i];
            uint16_t pri_sp = (uint16_t)((s - b->sprites) + b->zbuf_baseval);
            if (s->tile_width == s->total_width && s->tile_height == s->total_height) blit(b, s, zbuf, pri_sp);
            else blit_zoom(b, s, zbuf, pri_sp);
        }
        for (int pri2 = 0; pri2 <= 3; pri2++)
            for (int l = 0; l < 3; l++)
                if (v[l].on && (b->layer_vregs[l][2] & 3) == pri2) draw_layer(b, l, &v[l], (uint32_t)pri);
    }
}

void ga_video_render(const ga_board *b, uint32_t *pixels)
{
    memcpy(pixels, b->screen, sizeof(b->screen));
}
