/* render_seam (issue #183): with the camera pressed against a flat wall
 * and centred on a tile seam, every wall pixel is exactly the texel the
 * projection maths says it is, run through the column's colormap row.
 *
 * Scene: maps/doortest.vmap, north hall, facing north (angle 90 deg) at the
 * north wall face y = 8, cells (7,8) and (8,8) both test01, seam at x = 8.
 * Poses: x = 8.0 (centred on the seam) and 8.2 (the playtest pose), at
 * 0.2 and 0.25 units (PLAYER_RADIUS, the closest a player gets) from the
 * face, plus a few farther ones.
 *
 * Expected values are computed here from first principles, not read back
 * from the renderer: ray for column c is (cam, 1), cam = (c - 160) / 160;
 * the face y = 8 is at perpendicular distance d = 8 - py for every column,
 * hit at x = px + d * cam, texture u = frac(x) (north-facing faces are not
 * mirrored), tx = floor(64 u). Wall height h = VIEW_PROJ / d, top =
 * VIEW_H/2 - h/2 (eye 0.5), row y samples ty = floor((y + 0.5 - top) * 64/h).
 *
 * The dark stripe reported in #183 is the test01 fixture's own 1 px border
 * (tools/testpattern.py: darkest shade of the ramp, index 18 on columns 0
 * and 63). At a seam texel 63 of one tile meets texel 0 of the next, two
 * texels = 2 * h / 64 px: 20 px at d = 0.25, 25 px at d = 0.2. The test
 * pins that down: dark pixels appear only where tx is 0 or 63 (or on the
 * 16 px grid lines, which use a different index), and the stripe width is
 * exactly what two texels project to.
 *
 *   test_render_seam <maps/doortest.vmap> <assets/gen dir> */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bast.h"
#include "engine/colormap.h"
#include "engine/render.h"
#include "engine/video.h"

#define TEX 64

static uint8_t fb[SCREEN_W * SCREEN_H];

/* One pose: render, then check every column and every visible wall row. */
static void check_pose(const bast_map *map, const bast_tex *tex, double px, double py)
{
    const fix_t fx = (fix_t)lround(px * 65536.0), fy = (fix_t)lround(py * 65536.0);
    px = fx / 65536.0;
    py = fy / 65536.0;
    memset(fb, 216, sizeof fb);
    render_view(map, fx, fy, ANG90, fb);

    const double d = 8.0 - py;
    const double h = VIEW_PROJ / d;
    const double top = VIEW_H / 2.0 - h / 2.0;
    const uint8_t border = tex->pixels[0]; /* (0, 0): the 1 px border */
    int stripe = 0, mismatches = 0, foreign = 0;
    for (int c = 0; c < VIEW_W; c++) {
        const render_column *col = render_get_column(c);
        const double cam = (double)(c - VIEW_W / 2) / (VIEW_W / 2);
        const double hx = px + d * cam;
        double u = hx - floor(hx);
        int tx = (int)(u * TEX);
        /* Columns whose hit lands within 1e-9 of a texel edge are not
         * pinned to one side: either neighbour is a correct answer. */
        const double edge = u * TEX - floor(u * TEX + 1e-9);
        const int on_edge = fabs(edge) < 1e-9 || fabs(edge - 1) < 1e-9;
        assert(col->hit);
        assert(col->map_y == 8);
        assert(col->map_x == (int)floor(hx) || on_edge);
        assert(fabs(col->dist - d) < 1e-4);
        if (col->tex_x != tx) {
            if (!on_edge) {
                printf("pose (%.4f, %.4f) col %d: tex_x %d, expected %d (x %.6f)\n", px, py,
                       c, col->tex_x, tx, hx);
                mismatches++;
                continue;
            }
            tx = col->tex_x;
        }
        const uint8_t *cm = colormap_row(col->light_row);
        const uint8_t *src = tex->pixels + tx * TEX;
        /* Allowed colours for this column: its own texels, colormapped. */
        uint8_t allowed[256] = { 0 };
        for (int t = 0; t < TEX; t++)
            allowed[cm[src[t]]] = 1;
        int dark = 0;
        for (int y = col->top; y < col->bottom; y++) {
            const uint8_t got = fb[y * SCREEN_W + c];
            if (!allowed[got])
                foreign++;
            int ty = (int)floor((y + 0.5 - top) * TEX / h);
            if (ty < 0)
                ty = 0;
            if (ty > TEX - 1)
                ty = TEX - 1;
            /* Rows within one 16.16 step of a texel edge may round either
             * way; accept the neighbour there. */
            const double fy_t = (y + 0.5 - top) * TEX / h;
            const double frac = fy_t - floor(fy_t);
            const int ok = got == cm[src[ty]] ||
                           (frac < 1e-3 && ty > 0 && got == cm[src[ty - 1]]) ||
                           (frac > 1 - 1e-3 && ty < TEX - 1 && got == cm[src[ty + 1]]);
            if (!ok) {
                if (mismatches < 10)
                    printf("pose (%.4f, %.4f) col %d row %d: got %d, expected %d (tx %d ty %d)\n",
                           px, py, c, y, got, cm[src[ty]], tx, ty);
                mismatches++;
            }
            if (got == cm[border])
                dark++;
        }
        /* A full-height dark column only where the texture has one. */
        if (col->bottom - col->top > 0 && dark == col->bottom - col->top) {
            assert(tx == 0 || tx == TEX - 1);
            stripe++;
        }
    }
    /* Seam stripes on screen: each is 2 texels wide at d. Count seams the
     * view crosses and the px that two texels of them cover. */
    int expect = 0;
    for (int c = 0; c < VIEW_W; c++) {
        const double cam = (double)(c - VIEW_W / 2) / (VIEW_W / 2);
        const double hx = px + d * cam;
        const double u = hx - floor(hx);
        const int tx = (int)(u * TEX);
        expect += tx == 0 || tx == TEX - 1;
    }
    printf("pose (%.4f, %.4f): d %.4f h %.1f, dark stripe columns %d (expected %d), "
           "mismatches %d, foreign colours %d\n",
           px, py, d, h, stripe, expect, mismatches, foreign);
    fflush(stdout);
    assert(mismatches == 0);
    assert(foreign == 0);
    assert(abs(stripe - expect) <= 1);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    bast_map *map = bast_map_load(argv[1]);
    if (!map)
        fprintf(stderr, "%s\n", bast_error());
    assert(map);
    assert(colormap_load(argv[2]));
    if (!render_init(map, argv[2]))
        fprintf(stderr, "%s\n", render_error());
    assert(render_init(map, argv[2]));

    /* The scene is what the comment says: two test01 tiles meeting at x = 8
     * on the face y = 8, open cells in front of them. */
    const bast_map_cell *a = &map->cells[8 * map->width + 7];
    const bast_map_cell *b = &map->cells[8 * map->width + 8];
    assert(render_cell_is_wall(a) && render_cell_is_wall(b));
    assert(a->wall == b->wall);
    assert(strcmp(map->textures[a->wall], "test01") == 0);
    assert(!render_cell_is_wall(&map->cells[7 * map->width + 7]));
    assert(!render_cell_is_wall(&map->cells[7 * map->width + 8]));

    char path[1024];
    snprintf(path, sizeof path, "%s/test/test01.tex", argv[2]);
    bast_tex *tex = bast_tex_load(path);
    if (!tex)
        fprintf(stderr, "%s: %s\n", path, bast_error());
    assert(tex && tex->width == TEX && tex->height == TEX);
    /* The fixture's border: darkest shade on columns 0 and 63. */
    for (int y = 0; y < TEX; y++)
        assert(tex->pixels[y] == tex->pixels[0] && tex->pixels[63 * TEX + y] == tex->pixels[0]);

    const double xs[] = { 8.0, 8.2, 7.9, 8.05, 6.75 };
    const double ds[] = { 0.2, 0.25, 0.3, 0.5, 1.0 };
    for (size_t i = 0; i < sizeof xs / sizeof xs[0]; i++)
        for (size_t j = 0; j < sizeof ds / sizeof ds[0]; j++)
            check_pose(map, tex, xs[i], 8.0 - ds[j]);

    bast_tex_free(tex);
    render_shutdown();
    bast_map_free(map);
    puts("render_seam: ok");
    return 0;
}
