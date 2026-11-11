/* render_floor (issue #14): floors, ceilings and sky on maps/m1test.vmap.
 *
 * The expected values are worked out here from the geometry alone and
 * compared with the framebuffer:
 *
 * 1. Pose (2.5, 7.0) facing north (as render_walls). Screen pixel (sx, y)
 *    below the horizon looks along (cam, 1), cam = (sx - 160) / 160, and
 *    meets the floor (eye 0.5 units up, 160 px projection) at
 *    dist = 80 / (y + 0.5 - 104), i.e. world (2.5 + dist * cam, 7 + dist).
 *    Its colour is colormap row clamp(32 * dist / 16 + cell dark) of the
 *    floor texel u = frac(wx) * 64, v = 63 - frac(wy) * 64. Pixel
 *    (170, 143): cam 0.0625, dist = 80 / 39.5 = 2.0253, world
 *    (2.6266, 9.0253), cell (2, 9) test04, texel (40, 62), row 4. Checked for a block of pixels where
 *    the texel is not within 1/16 texel of a boundary.
 * 2. Same for the ceiling (row y above the horizon, ceiling texture), and
 *    in the NW dark room, where dark 16 is added per pixel.
 * 3. Sky, in the NE courtyard at (18.5, 19.5): ceiling rows 0..20 all
 *    land on sky cells. Each pixel is sky texel (render_sky_column, y),
 *    unshaded. render_sky_column moves by exactly 320 px (one full
 *    repeat: identical frame rows) for a 90 degree turn, 160 px for 45
 *    degrees and 5 px for 256 angle units. A 45 degree left turn shifts
 *    the drawn sky 160 px right on screen.
 *
 *   test_render_floor <maps/m1test.vmap> <assets/gen dir> */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "engine/colormap.h"
#include "engine/render.h"
#include "engine/video.h"

static uint8_t fb[SCREEN_W * SCREEN_H];
static uint8_t fb2[SCREEN_W * SCREEN_H];
static bast_tex *tex[BAST_MAP_MAX_TEXTURES];

static bast_tex *load(const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/test/%s.tex", dir, name);
    bast_tex *t = bast_tex_load(path);
    if (!t)
        fprintf(stderr, "%s\n", bast_error());
    assert(t);
    return t;
}

/* Expected colour of plane pixel (sx, y) from pose (px, py) facing (dx, dy).
 * Returns -1 when the texel is too close to a texel edge to call. */
static int expect_plane(const bast_map *m, double px, double py, double dx, double dy, int sx,
                        int y, int *cell_x, int *cell_y)
{
    const int floor_row = y >= VIEW_H / 2;
    const double rows = floor_row ? y + 0.5 - VIEW_H / 2 : VIEW_H / 2 - (y + 0.5);
    const double dist = 0.5 * VIEW_PROJ / rows;
    const double cam = (sx - VIEW_W / 2) / (double)(VIEW_W / 2);
    const double rx = dy, ry = -dx;
    const double wx = px + dist * (dx + rx * cam), wy = py + dist * (dy + ry * cam);
    const double fu = (wx - floor(wx)) * 64, fv = (wy - floor(wy)) * 64;
    const double eu = fu - floor(fu), ev = fv - floor(fv);
    if (eu < 1.0 / 16 || eu > 15.0 / 16 || ev < 1.0 / 16 || ev > 15.0 / 16)
        return -1;
    const int cx = (int)floor(wx), cy = (int)floor(wy);
    *cell_x = cx;
    *cell_y = cy;
    const bast_map_cell *c = &m->cells[cy * m->width + cx];
    const bast_tex *t = tex[floor_row ? c->floor : c->ceil];
    assert(t);
    int row = (int)(32.0 * dist / VIEW_DIST) + c->dark;
    if (row > 31)
        row = 31;
    const int u = (int)fu, v = 63 - (int)fv;
    return colormap_row(row)[t->pixels[u * 64 + v]];
}

/* Compare every open plane pixel in rows [y0, y1) x columns [x0, x1). */
static int check_block(const bast_map *m, double px, double py, angle_t a, int x0, int x1,
                       int y0, int y1, int want_dark)
{
    const double dx = fine_cos(a) / 65536.0, dy = fine_sin(a) / 65536.0;
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int sx = x0; sx < x1; sx++) {
            const render_column *col = render_get_column(sx);
            if (y >= col->top && y < col->bottom)
                continue;
            int cx, cy;
            const int want = expect_plane(m, px, py, dx, dy, sx, y, &cx, &cy);
            if (want < 0)
                continue;
            if (want_dark >= 0)
                assert(m->cells[cy * m->width + cx].dark == want_dark);
            if (fb[y * SCREEN_W + sx] != want) {
                fprintf(stderr, "pixel (%d,%d) cell (%d,%d): got %d want %d\n", sx, y, cx, cy,
                        fb[y * SCREEN_W + sx], want);
                assert(0);
            }
            n++;
        }
    return n;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    bast_map *map = bast_map_load(argv[1]);
    assert(map);
    assert(colormap_load(argv[2]));
    if (!render_init(map, argv[2]))
        fprintf(stderr, "%s\n", render_error());
    assert(render_init(map, argv[2]));
    for (int i = 1; i < map->texture_count; i++)
        tex[i] = load(argv[2], map->textures[i]);
    const bast_tex *sky = tex[map->sky_tex];
    assert(sky->width == RENDER_SKY_W && sky->height == RENDER_SKY_H);

    /* 1. The worked pixel, then a block of floor and ceiling. */
    const fix_t sx0 = fix_from_int(5) / 2, sy0 = fix_from_int(7);
    memset(fb, 216, sizeof fb);
    render_view(map, sx0, sy0, ANG90, fb);
    {
        const bast_tex *t04 = tex[bast_map_texture_index(map, "test04")];
        const int want = colormap_row(4)[t04->pixels[40 * 64 + 62]];
        printf("pixel (170,143): got %d want %d\n", fb[143 * SCREEN_W + 170], want);
        fflush(stdout);
        assert(fb[143 * SCREEN_W + 170] == want);
    }
    int n = check_block(map, 2.5, 7.0, ANG90, 0, VIEW_W, VIEW_H / 2, VIEW_H, -1);
    int m = check_block(map, 2.5, 7.0, ANG90, 0, VIEW_W, 0, VIEW_H / 2, -1);
    printf("start room: %d floor + %d ceiling pixels match\n", n, m);
    assert(n > 10000 && m > 10000);

    /* 2. NW dark room, cell dark 16 added per pixel (near rows only, so
     * every sample is inside the room). */
    render_view(map, fix_from_int(9) / 2, fix_from_int(39) / 2, ANG90, fb);
    n = check_block(map, 4.5, 19.5, ANG90, 0, VIEW_W, VIEW_H - 60, VIEW_H, 16);
    n += check_block(map, 4.5, 19.5, ANG90, 0, VIEW_W, 0, 60, 16);
    printf("dark room: %d pixels match with dark 16\n", n);
    assert(n > 10000);

    /* 3. Sky. */
    assert(render_sky_column(0, 0) == 0);
    for (int sx = 0; sx < VIEW_W; sx++) {
        for (int a = 0; a < 65536; a += 4096) {
            const angle_t b = (angle_t)a;
            assert(render_sky_column((angle_t)(b + ANG90), sx) == render_sky_column(b, sx));
            assert(render_sky_column((angle_t)(b + ANG45), sx) ==
                   (render_sky_column(b, sx) + RENDER_SKY_W - 160) % RENDER_SKY_W);
            assert(render_sky_column((angle_t)(b + 256), sx) ==
                   (render_sky_column(b, sx) + RENDER_SKY_W - 5) % RENDER_SKY_W);
        }
    }
    const fix_t cx = fix_from_int(37) / 2, cy = fix_from_int(39) / 2;
    const angle_t a0 = 3000;
    render_view(map, cx, cy, a0, fb);
    n = 0;
    for (int y = 0; y <= 20; y++)
        for (int sx = 0; sx < VIEW_W; sx++) {
            assert(render_get_column(sx)->top > 20);
            const int want = sky->pixels[render_sky_column(a0, sx) * RENDER_SKY_H + y];
            assert(fb[y * SCREEN_W + sx] == want);
            n++;
        }
    printf("sky: %d pixels match, column 0 at angle %d = sky column %d\n", n, a0,
           render_sky_column(a0, 0));
    memcpy(fb2, fb, sizeof fb);
    render_view(map, cx, cy, (angle_t)(a0 + ANG90), fb);
    for (int y = 0; y <= 20; y++)
        assert(memcmp(fb + y * SCREEN_W, fb2 + y * SCREEN_W, VIEW_W) == 0);
    render_view(map, cx, cy, (angle_t)(a0 + ANG45), fb);
    for (int y = 0; y <= 20; y++)
        for (int sx = 160; sx < VIEW_W; sx++)
            assert(fb[y * SCREEN_W + sx] == fb2[y * SCREEN_W + sx - 160]);
    printf("sky: 90 deg turn = same rows (320 px offset), 45 deg turn = 160 px shift\n");

    for (int i = 0; i < BAST_MAP_MAX_TEXTURES; i++)
        bast_tex_free(tex[i]);
    render_shutdown();
    bast_map_free(map);
    printf("render_floor: ok\n");
    return 0;
}
