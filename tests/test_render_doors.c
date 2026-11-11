/* render_doors (issue #116): the wall pass draws doors from live door state
 * (render_set_doors), not from the map flags, on maps/doortest.vmap.
 *
 * Pose: (2.5, 3.5) facing north (angle 90 deg), the corridor tile below
 * door D at (2, 4). The door's south face is y = 4, 0.5 units ahead.
 * Ray for column c: (cam, 1) with cam = (c - 160) / 160, so it crosses
 * y = 4 at x = 2.5 + cam / 2:
 *   c = 100: x 2.3125   c = 160: x 2.5   c = 220: x 2.6875
 * A door at open amount p keeps its panel on x in [2 + p, 3) (it slides
 * toward +x, the same way in the world from both sides), texture column
 * (x - 2 - p) * 64 seen from the south. Through the opening the ray goes
 * on: column 160 to the north hall's wall face y = 8 (dist 4.5), column
 * 100 to the corridor's west wall x = 2 at y 4.83 (cell (1, 4)).
 *
 *   test_render_doors <maps/doortest.vmap> <assets/gen dir> */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "engine/colormap.h"
#include "engine/render.h"
#include "engine/video.h"

static uint8_t fb[SCREEN_W * SCREEN_H];
static fix_t open_amt[64 * 64];

static const render_column *col(int c)
{
    const render_column *r = render_get_column(c);
    printf("col %3d: hit=%d dist=%.4f cell (%d,%d) tex=%d u=%d\n", c, r->hit, r->dist, r->map_x,
           r->map_y, r->tex, r->tex_x);
    fflush(stdout);
    assert(r->hit);
    return r;
}

static int is_door_d(const render_column *r)
{
    return r->map_x == 2 && r->map_y == 4;
}

static void view(const bast_map *map, fix_t x, fix_t y, angle_t a)
{
    render_view(map, x, y, a, fb);
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
    const fix_t px = fix_from_int(5) / 2, py = fix_from_int(7) / 2;

    /* No table: the map's initial state, door D closed, P (10, 4) open. */
    render_set_doors(NULL);
    view(map, px, py, ANG90);
    assert(is_door_d(col(160)) && col(160)->tex_x == 32);
    assert(is_door_d(col(220)) && col(220)->tex_x == 44);

    /* Table, every door closed (0). */
    memset(open_amt, 0, sizeof open_amt);
    render_set_doors(open_amt);
    view(map, px, py, ANG90);
    assert(is_door_d(col(100)) && col(100)->tex_x == 20);
    assert(is_door_d(col(160)));
    assert(is_door_d(col(220)) && col(220)->tex_x == 44);
    assert(col(160)->dist > 0.49f && col(160)->dist < 0.51f);

    /* Half open: the west half is a gap, the panel moved half a tile east
     * and shows its texture shifted by 32 columns. */
    open_amt[4 * w + 2] = FIX_ONE / 2;
    view(map, px, py, ANG90);
    assert(!is_door_d(col(100)) && col(100)->map_x == 1 && col(100)->map_y == 4);
    assert(is_door_d(col(220)) && col(220)->tex_x == 44 - 32);
    assert(col(220)->dist > 0.49f && col(220)->dist < 0.51f);

    /* Fully open: nothing drawn for the door, the north hall wall shows. */
    open_amt[4 * w + 2] = FIX_ONE;
    view(map, px, py, ANG90);
    assert(!is_door_d(col(160)) && col(160)->map_y == 8);
    assert(col(160)->dist > 4.49f && col(160)->dist < 4.51f);

    /* Same half-open door from the north side, (2.5, 5.5) facing south:
     * the door's north face is y = 5, 0.5 ahead, and the panel is still on
     * x in [2.5, 3), which is now screen left. Column 100 crosses y = 5 at
     * x 2.6875 (panel), column 220 at 2.3125 (gap). The texture reads left
     * to right from this side too: u = 1 - (x - 2 - p) = 0.8125 -> 52. */
    open_amt[4 * w + 2] = FIX_ONE / 2;
    view(map, px, fix_from_int(11) / 2, ANG270);
    assert(is_door_d(col(100)) && col(100)->tex_x == 52);
    assert(!is_door_d(col(220)));

    /* A START_OPEN door closed by a tag: door P (10, 4) open with no table,
     * drawn closed when the table says 0. */
    render_set_doors(NULL);
    view(map, fix_from_int(21) / 2, py, ANG90);
    assert(!(col(160)->map_x == 10 && col(160)->map_y == 4));
    memset(open_amt, 0, sizeof open_amt);
    render_set_doors(open_amt);
    view(map, fix_from_int(21) / 2, py, ANG90);
    assert(col(160)->map_x == 10 && col(160)->map_y == 4);

    render_set_doors(NULL);
    render_shutdown();
    bast_map_free(map);
    printf("render_doors: ok\n");
    return 0;
}
