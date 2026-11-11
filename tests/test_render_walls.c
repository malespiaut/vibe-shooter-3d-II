/* render_walls (issue #13): render maps/m1test.vmap headless from a fixed
 * pose and check the wall columns against heights worked out by hand.
 *
 * Pose: (2.5, 7.0) facing north (angle 90 deg). Wall height in px is
 * VIEW_PROJ / perpendicular distance, centred on row VIEW_H / 2 = 104.
 * Ray for column c: (cam, 1) with cam = (c - 160) / 160.
 *   c = 160, cam  0     : north wall face y = 12 at x 2.5, dist 5    -> 32 px
 *   c = 100, cam -0.375 : west wall face  x = 1 at y 11, dist 4      -> 40 px
 *   c =  40, cam -0.75  : west wall face  x = 1 at y 9,  dist 2      -> 80 px
 *   c = 220, cam +0.375 : south face y = 9 of the pillar at (3, 9),
 *                         hit at x 3.25, dist 2                      -> 80 px
 * The perpendicular distance is the same for every column hitting the
 * y = 12 face (no fisheye): checked on columns 140..180.
 *
 *   test_render_walls <maps/m1test.vmap> <assets/gen dir> */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "engine/colormap.h"
#include "engine/render.h"
#include "engine/video.h"

static uint8_t fb[SCREEN_W * SCREEN_H];

static void check(int c, int height, int light, int tex_x)
{
    const render_column *col = render_get_column(c);
    printf("col %3d: hit=%d dist=%.4f height=%d rows %d..%d light=%d tex=%d u=%d cell (%d,%d)\n",
           c, col->hit, col->dist, col->height, col->top, col->bottom, col->light_row, col->tex,
           col->tex_x, col->map_x, col->map_y);
    fflush(stdout);
    assert(col->hit);
    assert(col->height == height);
    assert(col->top == VIEW_H / 2 - height / 2 && col->bottom == VIEW_H / 2 + height / 2);
    assert(col->light_row == light);
    if (tex_x >= 0)
        assert(col->tex_x == tex_x);
    /* Rows just outside the wall span are drawn (ceiling / floor); their
     * texels are checked by render_floor. */
    assert(fb[(col->top - 1) * SCREEN_W + c] != 216);
    assert(fb[col->bottom * SCREEN_W + c] != 216);
    assert(fabsf(render_depth()[c] - col->dist) < 1e-6f);
}

/* Every pixel written and in the palette: no entry flagged reserved in
 * palette_flags.bin (reserved_00..20 = 216..236), and the status-bar area
 * left at 0. The shadow_00..02 entries (237..239) are real colormap entries
 * since art-bible.md #59 and are allowed. */
static void check_palette(const bast_palette *pal)
{
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        assert(!(pal->flags[fb[i]] & BAST_PAL_RESERVED));
        assert(fb[i] != BAST_INDEX_TRANSPARENT);
        if (i >= VIEW_H * SCREEN_W)
            assert(fb[i] == 0);
    }
    memset(fb, 216, sizeof fb);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    bast_map *map = bast_map_load(argv[1]);
    if (!map)
        fprintf(stderr, "%s\n", bast_error());
    assert(map);
    if (!colormap_load(argv[2]))
        fprintf(stderr, "%s\n", colormap_error());
    assert(colormap_load(argv[2]));
    char path[1024];
    snprintf(path, sizeof path, "%s/palette.bin", argv[2]);
    bast_palette *pal = bast_palette_load(path);
    assert(pal);
    /* Index 216 is reserved_00: a pixel left unwritten fails the check. */
    assert(pal->flags[216] & BAST_PAL_RESERVED);
    if (!render_init(map, argv[2]))
        fprintf(stderr, "%s\n", render_error());
    assert(render_init(map, argv[2]));

    memset(fb, 216, sizeof fb); /* reserved_00: any pixel left unwritten fails below */
    render_view(map, fix_from_int(5) / 2, fix_from_int(7), ANG90, fb);

    /* light = 32 * dist / 16 + dark of the open cell in front of the face. */
    check(160, 32, 10, 32);   /* front (2,11) dark 0; u = frac(2.5) */
    check(100, 40, 8, 0);     /* u = frac(11) */
    check(40, 80, 4, 0);      /* u = frac(9) */
    check(220, 80, 4, 16);    /* pillar face, u = frac(3.25) */
    assert(render_get_column(220)->map_x == 3 && render_get_column(220)->map_y == 9);
    assert(render_get_column(100)->map_x == 0 && render_get_column(40)->map_x == 0);

    const render_column *ref = render_get_column(160);
    for (int c = 140; c <= 180; c++) {
        const render_column *col = render_get_column(c);
        assert(col->map_y == 12 && col->height == ref->height);
    }

    check_palette(pal);

    /* NW dark room (dark 16): (4.5, 19.0) facing north, top wall face
     * y = 23 at dist 4 -> 40 px, light 8 + 16. */
    render_view(map, fix_from_int(9) / 2, fix_from_int(19), ANG90, fb);
    check(160, 40, 8 + 16, 32);
    check_palette(pal);
    /* Looking back down the corridor from the dark room's far end: the
     * deepest shading this map produces must still be in the palette. */
    render_view(map, fix_from_int(9) / 2, fix_from_int(45) / 2, ANG270, fb);
    check_palette(pal);

    render_shutdown();
    bast_palette_free(pal);
    bast_map_free(map);
    printf("render_walls: ok\n");
    return 0;
}
