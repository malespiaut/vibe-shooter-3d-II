#include "game/topdown.h"

#include <string.h>

#include "engine/video.h"

/* Palette indices (art-bible.md ramps; catlantis.gpl order). */
enum {
    PAL_BLACK = 0,
    PAL_WHITE = 1,
    PAL_GRAY = 2, /* gray_00..gray_15 = 2..17 */
    PAL_FB_YELLOW = 241,
};

static void put(uint8_t *framebuffer, int x, int y, uint8_t c)
{
    if (x >= 0 && y >= 0 && x < SCREEN_W && y < SCREEN_H)
        framebuffer[y * SCREEN_W + x] = c;
}

/* Dev top-down view: one square of `scale` px per tile, north up, walls in
 * the gray ramp (shade by wall texture), floor dark gray, open sky a step
 * lighter, player as a 3-pixel arrow (white centre, yellow tip). */
void topdown_draw(const game_state *gs, uint8_t *framebuffer)
{
    const bast_map *m = gs->map;
    int scale = SCREEN_W / m->width;
    if (SCREEN_H / m->height < scale)
        scale = SCREEN_H / m->height;
    if (scale < 1)
        scale = 1;
    const int ox = (SCREEN_W - m->width * scale) / 2;
    const int oy = (SCREEN_H - m->height * scale) / 2;

    memset(framebuffer, PAL_BLACK, (size_t)SCREEN_W * SCREEN_H);
    for (int ty = 0; ty < m->height; ty++) {
        for (int tx = 0; tx < m->width; tx++) {
            const bast_map_cell *c = &m->cells[ty * m->width + tx];
            uint8_t col;
            if (game_tile_blocks(gs, tx, ty))
                col = (uint8_t)(PAL_GRAY + 8 + (c->wall % 4) * 2);
            else
                col = (uint8_t)(PAL_GRAY + (c->ceil == 0 ? 3 : 1));
            const int sy = oy + (m->height - 1 - ty) * scale;
            for (int y = 0; y < scale; y++)
                for (int x = 0; x < scale; x++)
                    put(framebuffer, ox + tx * scale + x, sy + y, col);
        }
    }

    const player_t *p = &gs->player;
    const int px = ox + (int)(((int64_t)p->x * scale) >> FIX_SHIFT);
    const int py = oy + m->height * scale - 1 - (int)(((int64_t)p->y * scale) >> FIX_SHIFT);
    const fix_t c = fine_cos(p->angle), s = fine_sin(p->angle);
    for (int k = 2; k >= 1; k--) {
        const int dx = (int)(((int64_t)c * k + FIX_ONE / 2) >> FIX_SHIFT);
        const int dy = (int)(((int64_t)s * k + FIX_ONE / 2) >> FIX_SHIFT);
        put(framebuffer, px + dx, py - dy, PAL_FB_YELLOW);
    }
    put(framebuffer, px, py, PAL_WHITE);
}

