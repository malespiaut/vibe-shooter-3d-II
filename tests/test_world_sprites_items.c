/* world_sprites_items (issue #120): pickups drawn as world sprites from
 * live game state.
 *
 * 1. Table: every pickup entity type (32..37, 48..68) maps to a
 *    live sprite whose frames <name>_idle_d0_f0..f<frames-1>.tex exist in
 *    assets/gen/sprites (after tools/build_assets.sh), bottom-centre origin,
 *    and no frame f<frames> exists (the count matches the art). The one
 *    exception is the Ankh Pistol (32): the player starts with it and it
 *    has no world pickup art (#85 drew weapons 3..7 only).
 * 2. Shimmer: sprite_anim_frame() = (tic / 18) % frames.
 * 3. Live state, on a synthetic map (camera (2.5, 3.5) facing east, y grows north so view right is -y):
 *    - a Fish Snack at (5.5, 3.5) (dist 3) is drawn by render_view_things
 *      with its bottom row on the floor line 104 + 80/3 -> row 130 and
 *      ~16 * 160 / (3 * 64) = 13 px wide around column 160;
 *    - render_view (no things) never draws live sprites;
 *    - the 2-frame Golden Sardine (dist 1.5, every texel shown) changes between tic 0 and 18, and tic 36
 *      matches tic 0;
 *    - rendering leaves game_hash() unchanged;
 *    - after the player walks onto the fish (game_tic, items_touch) it is
 *      gone from items_render_things and from the frame the same tic.
 *
 *   test_world_sprites_items <assets/gen dir> */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bast.h"
#include "engine/colormap.h"
#include "engine/files.h"
#include "engine/render.h"
#include "engine/sprite.h"
#include "engine/video.h"
#include "game/items.h"
#include "game/state.h"

static uint8_t base[SCREEN_W * SCREEN_H], fb[SCREEN_W * SCREEN_H], fb2[SCREEN_W * SCREEN_H];
static render_thing things[RENDER_MAX_SPRITES];

static bast_tex *load(const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "sprites/%s.tex", name);
    int64_t n = 0;
    uint8_t *d = files_read(path, &n);
    if (!d)
        return NULL;
    bast_tex *t = bast_tex_from_memory(d, n);
    free(d);
    assert(t);
    return t;
}

static void check_table(void)
{
    int checked = 0;
    for (int type = 0; type < BAST_ENT_TYPE_LIMIT; type++) {
        if (!item_is_pickup(type) || !bast_entity_type_name(type))
            continue; /* 38..47 are reserved, not entity types */
        const sprite_def *def = sprite_def_for_entity(type);
        if (type == BAST_ENT_ANKH_PISTOL) {
            assert(def == NULL);
            continue;
        }
        if (!def)
            fprintf(stderr, "pickup type %d (%s) has no sprite\n", type,
                    bast_entity_type_name(type));
        assert(def && def->live);
        assert(def->frames >= 1 && def->frames <= SPRITE_MAX_FRAMES);
        char name[128];
        for (int f = 0; f <= def->frames; f++) {
            snprintf(name, sizeof name, "%s_idle_d0_f%d", def->name, f);
            bast_tex *t = load(name);
            if (f == def->frames) {
                assert(!t); /* no extra frame left unused */
                break;
            }
            if (!t)
                fprintf(stderr, "missing sprites/%s.tex\n", name);
            assert(t);
            assert(t->origin_x == t->width / 2 && t->origin_y == t->height);
            printf("%-20s -> %s_idle_d0_f%d.tex %dx%d\n", bast_entity_type_name(type),
                   def->name, f, t->width, t->height);
            bast_tex_free(t);
        }
        checked++;
    }
    assert(checked == 26); /* 33..37 and 48..68 */
    /* Non-pickups keep their own rules: the test fixture is static. */
    assert(sprite_def_for_entity(BAST_ENT_TEST_SPRITE) &&
           !sprite_def_for_entity(BAST_ENT_TEST_SPRITE)->live);
}

static void check_anim(void)
{
    assert(sprite_anim_frame(0, 1) == 0 && sprite_anim_frame(1000, 1) == 0);
    assert(sprite_anim_frame(0, 2) == 0 && sprite_anim_frame(17, 2) == 0);
    assert(sprite_anim_frame(18, 2) == 1 && sprite_anim_frame(35, 2) == 1);
    assert(sprite_anim_frame(36, 2) == 0);
}

static int add(bast_map *m, int type, double x, double y)
{
    int i = bast_map_add_entity(m);
    assert(i >= 0);
    bast_map_entity *e = &m->entities[i];
    e->type = (uint16_t)type;
    e->x = (int32_t)(x * 65536);
    e->y = (int32_t)(y * 65536);
    e->flags = BAST_ENT_SKILL_ALL;
    return i;
}

/* Rows/columns where a and b differ inside the view, or -1. */
static void diff_box(const uint8_t *a, const uint8_t *b, int x0, int x1, int *l, int *r,
                     int *t, int *bt)
{
    *l = *r = *t = *bt = -1;
    for (int y = 0; y < VIEW_H; y++)
        for (int x = x0; x < x1; x++)
            if (a[y * SCREEN_W + x] != b[y * SCREEN_W + x]) {
                if (*l < 0 || x < *l)
                    *l = x;
                if (x > *r)
                    *r = x;
                if (*t < 0)
                    *t = y;
                *bt = y;
            }
}

static void view(const game_state *gs, uint32_t tic, uint8_t *out)
{
    const int n = items_render_things(gs, things, RENDER_MAX_SPRITES);
    render_view_things(gs->map, gs->player.x, gs->player.y, gs->player.angle, things, n, tic,
                       out);
}

static void check_live(const char *assets)
{
    bast_map *m = bast_map_new(16, 8);
    assert(m);
    const int t1 = bast_map_texture_index(m, "test01");
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 16; x++)
            if (x == 0 || y == 0 || x == 15 || y == 7) {
                bast_map_cell *c = bast_map_cell_at(m, x, y);
                c->wall = (uint8_t)t1;
                c->flags = BAST_CELL_SOLID;
            }
    add(m, BAST_ENT_PLAYER_START, 2.5, 3.5);
    add(m, BAST_ENT_FISH_SNACK, 5.5, 3.5);        /* dead ahead, dist 3 */
    add(m, BAST_ENT_GOLDEN_SARDINE, 4.0, 3.0);    /* dist 1.5, lateral +0.5 (right) */
    add(m, BAST_ENT_HAIRBALL_SCATTERGUN, 7.5, 5.5); /* thinker pickup, dist 5, left */
    if (!render_init(m, assets))
        fprintf(stderr, "%s\n", render_error());
    assert(render_init(m, assets));

    static game_state gs;
    game_init(&gs, m, 1, 2, 0);
    assert(gs.player.x == (fix_t)(2.5 * 65536) && gs.player.angle == 0);
    const int n = items_render_things(&gs, things, RENDER_MAX_SPRITES);
    assert(n == 3);
    assert(things[0].type == BAST_ENT_FISH_SNACK && things[1].type == BAST_ENT_GOLDEN_SARDINE);
    assert(things[2].type == BAST_ENT_HAIRBALL_SCATTERGUN);

    const uint64_t h0 = game_hash(&gs);
    render_view(m, gs.player.x, gs.player.y, gs.player.angle, base);
    view(&gs, 0, fb);
    assert(game_hash(&gs) == h0);

    int l, r, t, b;
    /* The fish: centre columns. */
    diff_box(base, fb, 140, 180, &l, &r, &t, &b);
    printf("fish: columns %d..%d rows %d..%d (floor row 130)\n", l, r, t, b);
    assert(b == 130);
    assert(l >= 153 && r <= 167 && r - l + 1 >= 11);
    assert(b - t + 1 <= 10); /* 12 texels * 160 / 192 = 10 px */
    /* Sardine right of centre, scattergun left of it: both drawn. */
    diff_box(base, fb, 180, VIEW_W, &l, &r, &t, &b);
    printf("sardine: columns %d..%d rows %d..%d\n", l, r, t, b);
    fflush(stdout);
    assert(l >= 0 && b == 156); /* floor line 104 + 80/1.5 = 157.3: centre 156.5 is the last inside */
    diff_box(base, fb, 0, 140, &l, &r, &t, &b);
    printf("scattergun: columns %d..%d rows %d..%d\n", l, r, t, b);
    assert(l >= 0 && b == 119);

    /* Shimmer: the sardine (2 frames) changes at tic 18, back at 36; the
     * fish (1 frame) does not. */
    view(&gs, 18, fb2);
    diff_box(fb, fb2, 180, VIEW_W, &l, &r, &t, &b);
    assert(l >= 0);
    diff_box(fb, fb2, 0, 180, &l, &r, &t, &b);
    assert(l < 0);
    view(&gs, 36, fb2);
    assert(memcmp(fb, fb2, sizeof fb) == 0);

    /* Walk onto the fish: gone from live state and the frame, same tic. */
    gs.player.health = 50; /* a Fish Snack is only taken below the cap */
    gs.player.x = (fix_t)(5.5 * 65536) - 4096;
    ticcmd cmd = { 0, 0, 0, 0 };
    game_tic(&gs, &cmd);
    assert(gs.pickups[0].taken);
    assert(items_render_things(&gs, things, RENDER_MAX_SPRITES) == 2);
    gs.player.x = (fix_t)(2.5 * 65536);
    gs.player.momx = gs.player.momy = 0;
    view(&gs, 0, fb2);
    diff_box(base, fb2, 140, 180, &l, &r, &t, &b);
    assert(l < 0);
    printf("fish taken at tic %u: gone from the frame\n", gs.tic);

    render_shutdown();
    bast_map_free(m);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!colormap_load(argv[1]))
        fprintf(stderr, "%s\n", colormap_error());
    assert(colormap_load(argv[1]));
    assert(files_init(argv[1]));
    check_table();
    check_anim();
    check_live(argv[1]);
    puts("world_sprites_items: ok");
    return 0;
}
