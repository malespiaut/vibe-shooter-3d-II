/* Vibe Shooter 3D II entry point. M1: runs the 35 Hz simulation on a map and
 * shows it in the dev top-down view until the 3D renderer exists.
 *   game [--map <file.vmap>]       (default maps/m1test.vmap)
 * WASD / arrows move (left/right arrows turn), Shift runs, mouse X turns.
 * Quits on Esc or window close. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "engine/colormap.h"
#include "engine/video.h"
#include "game/state.h"
#include "platform/platform.h"

#ifndef VS3D_ASSETS_DIR
#define VS3D_ASSETS_DIR "assets/gen"
#endif
#ifndef VS3D_MAPS_DIR
#define VS3D_MAPS_DIR "maps"
#endif

/* Palette indices (art-bible.md ramps; catlantis.gpl order). */
enum {
    PAL_BLACK = 0,
    PAL_WHITE = 1,
    PAL_GRAY = 2, /* gray_00..gray_15 = 2..17 */
    PAL_FB_YELLOW = 241,
};

#define MOUSE_TURN 40 /* angle units per pixel of mouse X motion */
#define MAX_CATCHUP_TICS 10

static uint8_t framebuffer[SCREEN_W * SCREEN_H];

/* Audio: no mixer yet, so the engine answers every request with silence. */
static void audio_fill(int16_t *out, int frames, void *user)
{
    (void)user;
    memset(out, 0, (size_t)frames * PLAT_AUDIO_CHANNELS * sizeof(int16_t));
}

static void put(int x, int y, uint8_t c)
{
    if (x >= 0 && y >= 0 && x < SCREEN_W && y < SCREEN_H)
        framebuffer[y * SCREEN_W + x] = c;
}

/* Dev top-down view: one square of `scale` px per tile, north up, walls in
 * the gray ramp (shade by wall texture), floor dark gray, open sky a step
 * lighter, player as a 3-pixel arrow (white centre, yellow tip). */
static void draw_topdown(const game_state *gs)
{
    const bast_map *m = gs->map;
    int scale = SCREEN_W / m->width;
    if (SCREEN_H / m->height < scale)
        scale = SCREEN_H / m->height;
    if (scale < 1)
        scale = 1;
    const int ox = (SCREEN_W - m->width * scale) / 2;
    const int oy = (SCREEN_H - m->height * scale) / 2;

    memset(framebuffer, PAL_BLACK, sizeof framebuffer);
    for (int ty = 0; ty < m->height; ty++) {
        for (int tx = 0; tx < m->width; tx++) {
            const bast_map_cell *c = &m->cells[ty * m->width + tx];
            uint8_t col;
            if (game_tile_blocks(m, tx, ty))
                col = (uint8_t)(PAL_GRAY + 8 + (c->wall % 4) * 2);
            else
                col = (uint8_t)(PAL_GRAY + (c->ceil == 0 ? 3 : 1));
            const int sy = oy + (m->height - 1 - ty) * scale;
            for (int y = 0; y < scale; y++)
                for (int x = 0; x < scale; x++)
                    put(ox + tx * scale + x, sy + y, col);
        }
    }

    const player_t *p = &gs->player;
    const int px = ox + (int)(((int64_t)p->x * scale) >> FIX_SHIFT);
    const int py = oy + m->height * scale - 1 - (int)(((int64_t)p->y * scale) >> FIX_SHIFT);
    const fix_t c = fine_cos(p->angle), s = fine_sin(p->angle);
    for (int k = 2; k >= 1; k--) {
        const int dx = (int)(((int64_t)c * k + FIX_ONE / 2) >> FIX_SHIFT);
        const int dy = (int)(((int64_t)s * k + FIX_ONE / 2) >> FIX_SHIFT);
        put(px + dx, py - dy, PAL_FB_YELLOW);
    }
    put(px, py, PAL_WHITE);
}

static int clamp127(int v)
{
    return v > TICCMD_MAX ? TICCMD_MAX : v < -TICCMD_MAX ? -TICCMD_MAX : v;
}

int main(int argc, char **argv)
{
    const char *map_path = VS3D_MAPS_DIR "/m1test.vmap";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
            map_path = argv[++i];
        } else {
            fprintf(stderr, "usage: game [--map <file.vmap>]\n");
            return 1;
        }
    }

    /* Palette + light colormap, built by `cmake --build build`. */
    if (!colormap_load(VS3D_ASSETS_DIR)) {
        fprintf(stderr, "game: %s\n", colormap_error());
        return 1;
    }
    bast_map *map = bast_map_load(map_path);
    if (!map) {
        fprintf(stderr, "game: %s: %s\n", map_path, bast_error());
        return 1;
    }

    static game_state gs;
    game_init(&gs, map, 1, 0);

    if (!platform_init("Vibe Shooter 3D II", 3)) {
        fprintf(stderr, "game: %s\n", platform_error());
        bast_map_free(map);
        return 1;
    }
    if (!platform_audio_open(audio_fill, NULL))
        fprintf(stderr, "game: audio disabled: %s\n", platform_error());
    platform_set_relative_mouse(true);

    uint8_t held[PKEY_COUNT] = { 0 };
    int mouse_dx = 0;
    uint64_t start = platform_ticks_ms();
    uint32_t tics_done = 0; /* tics run since `start` */

    int running = 1;
    while (running) {
        plat_event ev;
        while (platform_poll_event(&ev)) {
            if (ev.type == PEV_QUIT)
                running = 0;
            else if (ev.type == PEV_KEY_DOWN && ev.key == PKEY_ESCAPE)
                running = 0;
            else if ((ev.type == PEV_KEY_DOWN || ev.type == PEV_KEY_UP) && ev.key > 0 &&
                     ev.key < PKEY_COUNT)
                held[ev.key] = ev.type == PEV_KEY_DOWN;
            else if (ev.type == PEV_MOUSE_MOVE)
                mouse_dx += ev.dx;
        }

        /* Whole tics owed at 35 Hz since start. After a stall, drop the
         * backlog instead of fast-forwarding. */
        uint64_t due = (platform_ticks_ms() - start) * TICRATE / 1000;
        if (due > tics_done + MAX_CATCHUP_TICS) {
            tics_done = (uint32_t)due - 1;
        }
        while (tics_done < due) {
            ticcmd cmd = { 0, 0, 0, 0 };
            int fwd = 0, side = 0, turn = 0;
            if (held['w'] || held[PKEY_UP])
                fwd += TICCMD_MAX;
            if (held['s'] || held[PKEY_DOWN])
                fwd -= TICCMD_MAX;
            if (held['d'])
                side += TICCMD_MAX;
            if (held['a'])
                side -= TICCMD_MAX;
            if (held[PKEY_LEFT])
                turn += PLAYER_TURN_SPEED;
            if (held[PKEY_RIGHT])
                turn -= PLAYER_TURN_SPEED;
            turn -= mouse_dx * MOUSE_TURN; /* mouse right = clockwise */
            mouse_dx = 0;
            if (turn > INT16_MAX)
                turn = INT16_MAX;
            if (turn < INT16_MIN)
                turn = INT16_MIN;
            cmd.fwd = (int8_t)clamp127(fwd);
            cmd.side = (int8_t)clamp127(side);
            cmd.turn = (int16_t)turn;
            if (held[PKEY_LSHIFT] || held[PKEY_RSHIFT])
                cmd.buttons |= BT_RUN;
            game_tic(&gs, &cmd);
            tics_done++;
        }

        draw_topdown(&gs);
        platform_present(framebuffer, colormap_palette());
        platform_sleep_ms(5);
    }

    platform_shutdown();
    bast_map_free(map);
    return 0;
}
