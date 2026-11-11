/* render_pushwalls (issue #121): the wall pass draws pushwalls from live
 * state (render_set_pushwalls) and shades walls, floors and ceilings from
 * live darkness (render_set_dark), on maps/tagstest.vmap.
 *
 * Pose: (3.5, 7.5) facing north (angle 90 deg), the hall tile below the
 * pushwall P at (3, 8). North of P: (3, 9) secret floor, (3, 10) floor,
 * (3, 11) wall. Column 160 looks straight up x = 3.5, so a block whose
 * south face is at y = b is hit at dist b - 7.5, texture column 32.
 *
 *   test_render_pushwalls <maps/tagstest.vmap> <assets/gen dir> */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "engine/colormap.h"
#include "engine/render.h"
#include "engine/video.h"

static uint8_t fb[SCREEN_W * SCREEN_H];
static uint8_t dark[64 * 64];

static const render_column *col(int c)
{
    const render_column *r = render_get_column(c);
    printf("col %3d: hit=%d dist=%.4f cell (%d,%d) tex=%d u=%d row=%d\n", c, r->hit, r->dist,
           r->map_x, r->map_y, r->tex, r->tex_x, r->light_row);
    fflush(stdout);
    assert(r->hit);
    return r;
}

static int near(float a, float b)
{
    return a > b - 0.01f && a < b + 0.01f;
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
    const int w = map->width;
    const int ptex = map->cells[8 * w + 3].wall;
    assert(ptex != 0 && (map->cells[8 * w + 3].flags & BAST_CELL_PUSHWALL));
    const fix_t px = fix_from_int(7) / 2, py = fix_from_int(15) / 2;

    /* No list: P drawn as a wall in its map cell. */
    render_set_pushwalls(NULL, 0);
    render_view(map, px, py, ANG90, fb);
    assert(col(160)->map_x == 3 && col(160)->map_y == 8 && near(col(160)->dist, 0.5f));

    /* Live block at rest in its map cell: the same picture. */
    render_pushwall pw = { fix_from_int(3), fix_from_int(8), ptex };
    render_set_pushwalls(&pw, 1);
    render_view(map, px, py, ANG90, fb);
    assert(col(160)->map_y == 8 && near(col(160)->dist, 0.5f) && col(160)->tex == ptex);
    assert(col(160)->tex_x == 32);

    /* Half a tile into its slide north: the face is mid-cell at y 8.5, the
     * texture moves with the block (u still 32 at x 3.5). */
    pw.y = fix_from_int(17) / 2;
    render_view(map, px, py, ANG90, fb);
    assert(near(col(160)->dist, 1.0f) && col(160)->tex == ptex && col(160)->tex_x == 32);
    /* Column 220 (ray (0.375, 1)) meets y 8.5 at x 3.875: still the block. */
    assert(near(col(220)->dist, 1.0f) && col(220)->tex == ptex);

    /* At (3, 10), two tiles on: P and the secret cell are open floor. */
    pw.y = fix_from_int(10);
    render_view(map, px, py, ANG90, fb);
    assert(col(160)->map_x == 3 && col(160)->map_y == 10 && near(col(160)->dist, 2.5f));

    /* An empty list leaves every PUSHWALL cell open: the ray reaches the
     * wall (3, 11). */
    render_set_pushwalls(&pw, 0);
    render_view(map, px, py, ANG90, fb);
    assert(col(160)->map_y == 11 && near(col(160)->dist, 3.5f));

    /* Live darkness: the wall face takes the dark of the open cell in front
     * (3, 7), the floor and ceiling pixels under the camera take their
     * cell's dark too. */
    render_set_pushwalls(NULL, 0);
    memset(dark, 0, sizeof dark);
    render_set_dark(dark);
    render_view(map, px, py, ANG90, fb);
    const int row0 = col(160)->light_row;
    const uint8_t floor0 = fb[(VIEW_H - 1) * SCREEN_W + 160], ceil0 = fb[160];
    dark[7 * w + 3] = 20;
    render_view(map, px, py, ANG90, fb);
    assert(col(160)->light_row == row0 + 20);
    const uint8_t floor1 = fb[(VIEW_H - 1) * SCREEN_W + 160], ceil1 = fb[160];
    printf("floor %d -> %d, ceil %d -> %d\n", floor0, floor1, ceil0, ceil1);
    assert(floor0 != floor1);
    /* NULL: back to the map's dark (0 in this hall). */
    render_set_dark(NULL);
    render_view(map, px, py, ANG90, fb);
    assert(col(160)->light_row == row0);
    assert(fb[(VIEW_H - 1) * SCREEN_W + 160] == floor0);

    render_shutdown();
    bast_map_free(map);
    printf("render_pushwalls: ok\n");
    return 0;
}
