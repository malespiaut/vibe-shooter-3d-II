/* Weapon view-model (weapons.md View-model, Behaviour notes, Bob; issue
 * #100): bob table, switch lower/raise, fire frame timing driven by real
 * game tics, muzzle flash light shift (also in the renderer) and drawing.
 * Usage: test_viewmodel <bob|switch|frames|flash|draw> <assets/gen> <map.vmap> */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "engine/colormap.h"
#include "engine/render.h"
#include "engine/video.h"
#include "game/state.h"
#include "game/viewmodel.h"

static const char *assets;
static const char *map_path;
static const ticcmd IDLE = { 0, 0, 0, 0 };
static const ticcmd FIRE = { 0, 0, 0, BT_FIRE };

/* 32x8 map, solid border. */
static bast_map *open_map(void)
{
    bast_map *m = bast_map_new(32, 8);
    assert(m);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 32; x++)
            if (x == 0 || y == 0 || x == 31 || y == 7)
                bast_map_cell_at(m, x, y)->flags = BAST_CELL_SOLID;
    return m;
}

static void setup(game_state *gs, const bast_map *m, int wp)
{
    game_init(gs, m, 1, 2, 0);
    gs->player.x = fix_from_int(2) + FIX_ONE / 2;
    gs->player.y = fix_from_int(3) + FIX_ONE / 2;
    gs->player.angle = 0;
    weapons_t *w = &gs->player.weapons;
    w->owned |= 1u << wp;
    w->ready = (uint8_t)wp;
    w->ammo[AM_CHARGES] = 200;
    w->ammo[AM_SHELLS] = 50;
}

static void test_bob(void)
{
    /* At walk speed: x = 6 sin(p), y = 4 + 4 sin(2p) (y down). */
    static const struct { angle_t p; int x, y; } t[] = {
        { 0, 0, 4 },          /* phase 0 */
        { ANG90, 6, 4 },      /* 1/4 */
        { ANG180, 0, 4 },     /* 1/2 */
        { ANG270, -6, 4 },    /* 3/4 */
        { ANG45, 4, 8 },      /* 1/8: 6 * 0.707 = 4.24, 4 + 4 */
        { ANG90 + ANG45, 4, 0 }, /* 3/8: sin(2p) = -1, lowest lift */
        { ANG270 + ANG45, -4, 0 },
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        int dx, dy;
        viewmodel_bob(t[i].p, PLAYER_WALK_SPEED, &dx, &dy);
        printf("bob phase %5u: (%d, %d) expect (%d, %d)\n", t[i].p, dx, dy, t[i].x, t[i].y);
        assert(dx == t[i].x && dy == t[i].y);
        /* Running caps at the walk amplitude. */
        int rx, ry;
        viewmodel_bob(t[i].p, PLAYER_RUN_SPEED, &rx, &ry);
        assert(rx == dx && ry == dy);
    }
    /* Range over the whole table: x in [-6, 6], y in [0, 8]. */
    for (int p = 0; p < 65536; p += 16) {
        int dx, dy;
        viewmodel_bob((angle_t)p, PLAYER_WALK_SPEED, &dx, &dy);
        assert(dx >= -VM_BOB_X && dx <= VM_BOB_X && dy >= 0 && dy <= 2 * VM_BOB_Y);
    }
    /* Standing still: flush at any phase. Half walk speed: half amplitude. */
    int dx, dy;
    viewmodel_bob(ANG90, 0, &dx, &dy);
    assert(dx == 0 && dy == 0);
    viewmodel_bob(ANG90, PLAYER_WALK_SPEED / 2, &dx, &dy);
    assert(dx == 3 && dy == 2);
}

static void test_switch(void)
{
    /* Pure function: lower 5, raise 5, fully down at the swap. */
    static const int h = 72; /* pistol */
    static const int expect[11] = { 0, 14, 28, 43, 57, 72, 57, 43, 28, 14, 0 }; /* by switch_tics */
    for (int s = 0; s <= 10; s++)
        assert(viewmodel_drop(h, s) == expect[s]);

    /* Real tics: pistol -> claws. */
    bast_map *m = open_map();
    game_state gs;
    setup(&gs, m, WP_PISTOL);
    ticcmd sel = { 0, 0, 0, (uint8_t)(WP_CLAWS << BT_WEAPON_SHIFT) };
    game_tic(&gs, &sel);
    const weapons_t *w = &gs.player.weapons;
    for (int tic = 1; tic <= 11; tic++) {
        const int wp = viewmodel_weapon(w);
        const int hh = wp == WP_PISTOL ? 72 : 64;
        const int d = viewmodel_drop(hh, w->switch_tics);
        printf("switch tic %2d: switch_tics %d weapon %d drop %d/%d\n", tic, w->switch_tics, wp,
               d, hh);
        if (tic < 5) { /* old weapon going down */
            assert(wp == WP_PISTOL && d == hh * tic / 5);
        } else if (tic == 5) { /* mid-switch: the swap tic, fully lowered */
            assert(wp == WP_CLAWS && d == hh);
        } else { /* new weapon coming up */
            assert(wp == WP_CLAWS && d == hh * (10 - tic < 0 ? 0 : 10 - tic) / 5);
        }
        game_tic(&gs, &IDLE);
    }
    /* Half the height after half the lowering: 2.5 tics of 5. */
    assert(viewmodel_drop(64, 8) < 32 && viewmodel_drop(64, 7) > 32);
    bast_map_free(m);
}

static void check_sequence(int wp, const char *const *expect, int n)
{
    bast_map *m = open_map();
    game_state gs;
    setup(&gs, m, wp);
    game_tic(&gs, &FIRE);
    for (int i = 0; i < n; i++) {
        const char *f = viewmodel_frame_of(&gs.player.weapons);
        printf("weapon %d e=%2d: %s\n", wp, i, f);
        assert(strcmp(f, expect[i]) == 0);
        game_tic(&gs, &IDLE);
    }
    bast_map_free(m);
}

static void test_frames(void)
{
    /* One pistol shot: fire0 2 tics, fire1 4 tics, then idle. */
    static const char *const pistol[] = { "fire0", "fire0", "fire1", "fire1", "fire1",
                                          "fire1", "idle", "idle", "idle", "idle",
                                          "idle", "idle", "idle", "idle" };
    check_sequence(WP_PISTOL, pistol, 14);
    /* Scattergun: fire0 3, fire1 5, idle 8, pump0 6 (from the pump sound
     * tic), pump1 6, idle. */
    const char *scatter[34];
    for (int e = 0; e < 34; e++)
        scatter[e] = e < 3 ? "fire0" : e < 8 ? "fire1" : e < 16 ? "idle" : e < 22 ? "pump0"
                   : e < 28 ? "pump1" : "idle";
    check_sequence(WP_SCATTERGUN, scatter, 34);
    /* Every sequence fits its weapon's fire interval. */
    for (int wp = WP_CLAWS; wp < WP_SLOTS; wp++) {
        assert(strcmp(viewmodel_frame(wp, weapon_info[wp].interval), "idle") == 0);
        assert(strcmp(viewmodel_frame(wp, 0), "idle") != 0);
    }
    /* Repeater held: fire0 fire0 fire1 fire1, again. */
    assert(strcmp(viewmodel_frame(WP_REPEATER, 1), "fire0") == 0);
    assert(strcmp(viewmodel_frame(WP_REPEATER, 2), "fire1") == 0);
}

static void test_flash(void)
{
    bast_map *m = open_map();
    game_state gs;
    setup(&gs, m, WP_PISTOL);
    assert(viewmodel_light_shift(&gs.player.weapons) == 0);
    game_tic(&gs, &FIRE);
    static const int expect[] = { -4, -4, -4, 0, 0 };
    for (int i = 0; i < 5; i++) {
        printf("flash tic %d: shift %d\n", i, viewmodel_light_shift(&gs.player.weapons));
        assert(viewmodel_light_shift(&gs.player.weapons) == expect[i]);
        game_tic(&gs, &IDLE);
    }
    /* Claws never flash. */
    setup(&gs, m, WP_CLAWS);
    game_tic(&gs, &FIRE);
    assert(viewmodel_light_shift(&gs.player.weapons) == 0);
    bast_map_free(m);

    /* Renderer: every wall column 4 rows brighter, clamped at 0. */
    assert(colormap_load(assets));
    bast_map *map = bast_map_load(map_path);
    assert(map);
    assert(render_init(map, assets));
    static uint8_t fb[SCREEN_W * SCREEN_H];
    static int base[VIEW_W];
    const fix_t x = fix_from_int(5) / 2, y = fix_from_int(7);
    render_view(map, x, y, ANG90, fb);
    for (int c = 0; c < VIEW_W; c++)
        base[c] = render_get_column(c)->light_row;
    render_set_light_shift(-VM_FLASH_ROWS);
    render_view(map, x, y, ANG90, fb);
    int changed = 0;
    for (int c = 0; c < VIEW_W; c++) {
        const render_column *col = render_get_column(c);
        if (!col->hit)
            continue;
        const int want = base[c] - 4 < 0 ? 0 : base[c] - 4;
        assert(col->light_row == want);
        changed += col->light_row != base[c];
    }
    printf("flash: %d of %d wall columns brighter\n", changed, VIEW_W);
    assert(changed > 0);
    render_set_light_shift(0);
    render_view(map, x, y, ANG90, fb);
    for (int c = 0; c < VIEW_W; c++)
        assert(render_get_column(c)->light_row == base[c]);
    render_shutdown();
    bast_map_free(map);
}

static void bbox(const uint8_t *fb, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = SCREEN_W, *y0 = SCREEN_H, *x1 = -1, *y1 = -1;
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            if (fb[y * SCREEN_W + x] != 0) {
                if (x < *x0) *x0 = x;
                if (x > *x1) *x1 = x;
                if (y < *y0) *y0 = y;
                if (y > *y1) *y1 = y;
            }
}

static void test_draw(void)
{
    assert(viewmodel_init(assets));
    bast_map *m = open_map();
    game_state gs;
    setup(&gs, m, WP_PISTOL);
    static uint8_t fb[SCREEN_W * SCREEN_H];
    int x0, y0, x1, y1;

    /* Pistol idle at rest: inside the 64x72 box bottom-centred on the view. */
    memset(fb, 0, sizeof fb);
    viewmodel_draw(fb, &gs);
    bbox(fb, &x0, &y0, &x1, &y1);
    printf("pistol idle bbox (%d,%d)-(%d,%d)\n", x0, y0, x1, y1);
    assert(x0 >= 160 - 32 && x1 < 160 + 32 && y0 >= VIEW_H - 72 && y1 == VIEW_H - 1);

    /* Walking at phase 1/8: 4 px right, 8 px down, still clipped to the view. */
    gs.player.momx = PLAYER_WALK_SPEED;
    gs.player.weapons.bob_phase = ANG45;
    memset(fb, 0, sizeof fb);
    viewmodel_draw(fb, &gs);
    int bx0, by0, bx1, by1;
    bbox(fb, &bx0, &by0, &bx1, &by1);
    printf("pistol bob bbox (%d,%d)-(%d,%d)\n", bx0, by0, bx1, by1);
    assert(bx0 == x0 + 4 && by0 == y0 + 8 && by1 == VIEW_H - 1);
    gs.player.momx = 0;

    /* Mid-switch fully lowered: nothing drawn. */
    gs.player.weapons.switch_tics = 5;
    memset(fb, 0, sizeof fb);
    viewmodel_draw(fb, &gs);
    bbox(fb, &x0, &y0, &x1, &y1);
    assert(x1 < 0);
    gs.player.weapons.switch_tics = 0;

    /* Weapons whose art has not landed: skipped, no crash. */
    for (int wp = WP_LAUNCHER; wp <= WP_HOWLER; wp++) {
        gs.player.weapons.ready = (uint8_t)wp;
        memset(fb, 0, sizeof fb);
        viewmodel_draw(fb, &gs);
    }
    /* Second init: no second warning (stderr), no leak. */
    assert(viewmodel_init(assets));
    viewmodel_shutdown();
    bast_map_free(m);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: test_viewmodel <group> <assets/gen> [map.vmap]\n");
        return 2;
    }
    assets = argv[2];
    map_path = argc > 3 ? argv[3] : NULL;
    const char *g = argv[1];
    if (strcmp(g, "bob") == 0)
        test_bob();
    else if (strcmp(g, "switch") == 0)
        test_switch();
    else if (strcmp(g, "frames") == 0)
        test_frames();
    else if (strcmp(g, "flash") == 0)
        test_flash();
    else if (strcmp(g, "draw") == 0)
        test_draw();
    else
        return 2;
    printf("viewmodel_%s ok\n", g);
    return 0;
}
