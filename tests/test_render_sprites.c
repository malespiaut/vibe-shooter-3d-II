/* render_sprites (issue #15): billboard sprites against a synthetic map.
 *
 * Map 16x8, solid test01 border, one solid pillar at cell (5, 3). Camera at
 * (2.5, 3.5) facing east: forward (1, 0), view right = south. The pillar's
 * near face x = 5 is at dist 2.5 and spans lateral [-0.5, +0.5], screen
 * columns (128, 192].
 *
 * 1. Occlusion: a 64x64 fullbright sprite (index SPR) at (8.0, 4.5): dist
 *    5.5, lateral -1.0, so it spans columns ~116..145 and the pillar's left
 *    edge cuts it at column 128. In each column it is drawn iff the wall
 *    depth there is farther than the sprite; at the edge the last visible
 *    column is 128 and the next one, 129, is the pillar.
 * 2. Size: the same sprite alone at dist d in open space is 160/d px tall
 *    (within 1 px), its bottom row on the floor line 104 + 80/d.
 * 3. Shading: not fullbright, cell dark 3 at dist 5.5: every pixel is
 *    colormap row clamp(32 * 5.5 / 16 + 3) = 14 of SPR.
 * 4. Transparency: index 255 pixels leave the wall behind untouched.
 * 5. Sort: two overlapping sprites draw far to near whatever the list order.
 *
 *   test_render_sprites <assets/gen dir> */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bast.h"
#include "engine/colormap.h"
#include "engine/render.h"
#include "engine/video.h"

#define SPR 100  /* a red-ramp index: not in test01, not a flat fill */
#define SPR2 140 /* violet ramp */

static uint8_t fb[SCREEN_W * SCREEN_H];
static uint8_t walls_only[SCREEN_W * SCREEN_H];

static bast_tex *solid_tex(int w, int h, uint8_t idx, int flags)
{
    bast_tex *t = bast_tex_new(w, h);
    assert(t);
    memset(t->pixels, idx, (size_t)w * h);
    t->origin_x = (int16_t)(w / 2);
    t->origin_y = (int16_t)h;
    t->flags = (uint8_t)flags;
    return t;
}

/* Rows in column c that hold value v: count, first, last. */
static int rows_of(int c, uint8_t v, int *first, int *last)
{
    int n = 0;
    *first = -1;
    *last = -1;
    for (int y = 0; y < VIEW_H; y++)
        if (fb[y * SCREEN_W + c] == v) {
            if (*first < 0)
                *first = y;
            *last = y;
            n++;
        }
    return n;
}

static void view(const bast_map *m, double x, double y, angle_t a)
{
    render_view(m, (fix_t)(x * 65536), (fix_t)(y * 65536), a, fb);
    memcpy(walls_only, fb, sizeof fb);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!colormap_load(argv[1]))
        fprintf(stderr, "%s\n", colormap_error());
    assert(colormap_load(argv[1]));

    bast_map *m = bast_map_new(16, 8);
    assert(m);
    int t1 = bast_map_texture_index(m, "test01");
    assert(t1 == 1);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 16; x++) {
            bast_map_cell *c = bast_map_cell_at(m, x, y);
            if (x == 0 || y == 0 || x == 15 || y == 7 || (x == 5 && y == 3)) {
                c->wall = (uint8_t)t1;
                c->flags = BAST_CELL_SOLID;
            }
        }
    bast_map_cell_at(m, 8, 4)->dark = 3;
    if (!render_init(m, argv[1]))
        fprintf(stderr, "%s\n", render_error());
    assert(render_init(m, argv[1]));

    bast_tex *full = solid_tex(64, 64, SPR, BAST_TEX_FULLBRIGHT);
    bast_tex *lit = solid_tex(64, 64, SPR, 0);

    /* 1. Occlusion by the pillar. */
    view(m, 2.5, 3.5, 0);
    for (int c = 0; c < VIEW_W; c++)
        for (int y = 0; y < VIEW_H; y++)
            assert(fb[y * SCREEN_W + c] != SPR);
    render_sprite s = { fix_from_int(8), fix_from_int(9) / 2, 0, full, 3 };
    assert(render_draw_sprites(&s, 1, fb) == 1);
    const float z = 5.5f;
    const float *depth = render_depth();
    int drawn_cols = 0, hidden_cols = 0, edge = -1;
    for (int c = 0; c < VIEW_W; c++) {
        int f, l;
        int n = rows_of(c, SPR, &f, &l);
        double centre = 160.0 - 160.0 / 5.5;
        double half = 0.5 * 160.0 / 5.5;
        int inside = c + 0.5 >= centre - half + 0.01 && c + 0.5 < centre + half - 0.01;
        if (!inside)
            continue;
        if (depth[c] > z) {
            assert(n > 0);
            drawn_cols++;
        } else {
            assert(n == 0);
            /* The wall pixels are the walls-only render, untouched. */
            for (int y = 0; y < VIEW_H; y++)
                assert(fb[y * SCREEN_W + c] == walls_only[y * SCREEN_W + c]);
            hidden_cols++;
            if (edge < 0)
                edge = c;
        }
    }
    printf("occlusion: %d columns drawn, %d hidden behind the pillar, first hidden %d "
           "(depth %.3f, previous %.3f)\n",
           drawn_cols, hidden_cols, edge, depth[edge], depth[edge - 1]);
    assert(drawn_cols >= 10 && hidden_cols >= 10);
    /* The pillar's left edge: walls cast column c at cam (c - 160) / 160,
     * so the ray of column 128 grazes y = 4 exactly and passes; 129 is the
     * first pillar column. */
    assert(edge == 129);
    {
        int f, l;
        assert(rows_of(edge - 1, SPR, &f, &l) > 0); /* visible in the column before... */
        assert(rows_of(edge, SPR, &f, &l) == 0);    /* ...hidden in the pillar column */
        assert(render_get_column(edge)->map_x == 5 && render_get_column(edge)->map_y == 3);
        assert(render_get_column(edge - 1)->map_x == 15);
    }

    /* 2. Height 160/d at ten distances, bottom on the floor line. The
     * camera at (1.5, 5.5) looks east down the open row y = 5. */
    for (int i = 1; i <= 10; i++) {
        const double d = i == 1 ? 1.25 : i * 1.2;
        view(m, 1.5, 5.5, 0);
        render_sprite p = { (fix_t)((1.5 + d) * 65536), (fix_t)(5.5 * 65536), 0, full, 0 };
        assert(render_draw_sprites(&p, 1, fb) == 1);
        int f, l;
        int n = rows_of(160, SPR, &f, &l);
        double want = 160.0 / d, floor_line = 104.0 + 80.0 / d;
        printf("size: dist %.2f: %d px tall (160/d = %.2f), rows %d..%d, floor line %.2f\n", d, n,
               want, f, l, floor_line);
        assert(fabs(n - want) <= 1.0);
        assert(l - f + 1 == n); /* one solid span */
        assert(fabs((l + 1) - floor_line) <= 1.0);
        /* Width matches height for a square TEX. */
        int w = 0;
        for (int c = 0; c < VIEW_W; c++)
            w += fb[(f + l) / 2 * SCREEN_W + c] == SPR;
        assert(fabs(w - want) <= 1.0);
    }

    /* 3. Shading: one colormap row per sprite from its cell's dark. */
    view(m, 2.5, 3.5, 0);
    {
        render_sprite q = { fix_from_int(8), fix_from_int(9) / 2, 0, lit, 3 };
        render_draw_sprites(&q, 1, fb);
        const uint8_t want = colormap_row(14)[SPR];
        assert(want != SPR);
        int n = 0;
        for (int i = 0; i < VIEW_W * VIEW_H; i++)
            if (fb[i] != walls_only[i]) {
                assert(fb[i] == want);
                n++;
            }
        printf("shading: %d px, all colormap row 14 of %d = %d\n", n, SPR, want);
        assert(n > 100);
    }

    /* 4. Transparency: left half 255 leaves the wall behind visible. */
    view(m, 2.5, 5.5, 0);
    {
        bast_tex *half = solid_tex(64, 64, SPR, BAST_TEX_FULLBRIGHT | BAST_TEX_HAS_TRANSPARENCY);
        memset(half->pixels, BAST_INDEX_TRANSPARENT, 32 * 64);
        render_sprite q = { fix_from_int(10), fix_from_int(11) / 2, 0, half, 0 };
        render_draw_sprites(&q, 1, fb);
        /* dist 7.5, centred: columns [160 - 10.67, 160) are transparent. */
        for (int c = 150; c < 160; c++)
            for (int y = 0; y < VIEW_H; y++)
                assert(fb[y * SCREEN_W + c] == walls_only[y * SCREEN_W + c]);
        int f, l;
        assert(rows_of(161, SPR, &f, &l) > 0);
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
            assert(fb[i] != BAST_INDEX_TRANSPARENT);
        bast_tex_free(half);
        printf("transparency: ok\n");
    }

    /* 5. Far to near regardless of list order: the near one wins where
     * they overlap, the far one shows beside it. */
    {
        bast_tex *far = solid_tex(64, 64, SPR2, BAST_TEX_FULLBRIGHT);
        render_sprite pair[2][2] = {
            { { fix_from_int(10), fix_from_int(5), 0, far, 0 },
              { fix_from_int(6), fix_from_int(5) + 24576, 0, full, 0 } },
            { { fix_from_int(6), fix_from_int(5) + 24576, 0, full, 0 },
              { fix_from_int(10), fix_from_int(5), 0, far, 0 } },
        };
        uint8_t first[SCREEN_W * SCREEN_H];
        for (int k = 0; k < 2; k++) {
            view(m, 1.5, 5.0, 0);
            assert(render_draw_sprites(pair[k], 2, fb) == 2);
            assert(fb[104 * SCREEN_W + 160] == SPR);
            int saw_far = 0;
            for (int c = 0; c < VIEW_W; c++)
                saw_far |= fb[104 * SCREEN_W + c] == SPR2;
            assert(saw_far);
            if (k == 0)
                memcpy(first, fb, sizeof fb);
            else
                assert(memcmp(first, fb, sizeof fb) == 0);
        }
        bast_tex_free(far);
        printf("sort: ok\n");
    }

    /* Behind the camera: not drawn. */
    view(m, 8.5, 5.5, 0);
    {
        render_sprite b = { fix_from_int(4), fix_from_int(5), 0, full, 0 };
        assert(render_draw_sprites(&b, 1, fb) == 0);
        assert(memcmp(fb, walls_only, sizeof fb) == 0);
    }

    bast_tex_free(full);
    bast_tex_free(lit);
    render_shutdown();
    bast_map_free(m);
    printf("render_sprites: ok\n");
    return 0;
}
