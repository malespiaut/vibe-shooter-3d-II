/* goto around things (issue #161): nav_goto plans around tiles whose centre
 * holds a live thing (thinker_alive: enemies, barrels), never blocks the
 * goal or the player's tile, falls back to the tile-only path when things
 * close every route, and avoids a tile it stalled against for
 * NAV_STALL_TICS. Maps are built in memory with libbast, as test_ai.
 * Usage: test_demo_nav <group>. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "game/demo_nav.h"
#include "game/state.h"

#define FX(i) fix_from_int(i)
#define CENTRE(i) (FX(i) + FIX_ONE / 2)

static bast_map *open_map(int w, int h)
{
    bast_map *m = bast_map_new(w, h);
    assert(m);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1)
                bast_map_cell_at(m, x, y)->flags = BAST_CELL_SOLID;
    return m;
}

static void add_entity(bast_map *m, uint16_t type, int tx, int ty, angle_t angle, uint16_t flags)
{
    int i = bast_map_add_entity(m);
    assert(i >= 0);
    m->entities[i].type = type;
    m->entities[i].x = CENTRE(tx);
    m->entities[i].y = CENTRE(ty);
    m->entities[i].angle = angle;
    m->entities[i].flags = BAST_ENT_SKILL_ALL | flags;
}

static int tile_x(const game_state *gs) { return fix_to_int(gs->player.x); }
static int tile_y(const game_state *gs) { return fix_to_int(gs->player.y); }

/* Runs goto to (tx, ty) for at most `max` tics; returns the tics used, or
 * -1 on timeout / no path. Asserts the player never enters `never` tile. */
static int stalled; /* run_goto saw ns.avoid set */

static int run_goto(game_state *gs, int tx, int ty, int max, int nx, int ny)
{
    nav_state ns;
    nav_reset(&ns);
    stalled = 0;
    for (int t = 0; t <= max; t++) {
        ticcmd cmd;
        int s = nav_goto(gs, tx, ty, &ns, &cmd);
        if (s == NAV_ARRIVED)
            return t;
        if (s == NAV_NO_PATH)
            return -1;
        stalled |= ns.avoid >= 0;
        game_tic(gs, &cmd);
        assert(!(tile_x(gs) == nx && tile_y(gs) == ny));
    }
    return -1;
}

/* Open 18x9 room: the straight (tile-only) path from (2, 5) east to (14, 5)
 * runs through an AMBUSH Mutt at (8, 5) facing away. goto walks around it
 * and arrives without touching its tile. */
static void test_around(void)
{
    bast_map *m = open_map(20, 11);
    add_entity(m, BAST_ENT_PLAYER_START, 2, 5, 0, 0);
    add_entity(m, BAST_ENT_MUTT_TROOPER, 8, 5, 0, BAST_ENT_AMBUSH);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    int t = run_goto(&gs, 14, 5, 600, 8, 5);
    printf("around: arrived after %d tics, stalled=%d\n", t, stalled);
    assert(!stalled); /* the plan saw the Mutt: no push into it */
    assert(t > 0 && t < 300);
    assert(tile_x(&gs) == 14 && tile_y(&gs) == 5);
    assert(thinker_alive(&gs.things.list[0])); /* walked around, not through */
    bast_map_free(m);
}

/* A 1-wide corridor y = 2 from x = 1 to 10, a barrel at (5, 2) in it. The
 * only route goes through the barrel: goto falls back to the tile-only
 * path (it moves, it does not report NO_PATH), and the goal tile holding a
 * barrel is never refused either. */
static void test_fallback(void)
{
    bast_map *m = open_map(12, 5);
    for (int x = 1; x <= 10; x++) {
        bast_map_cell_at(m, x, 1)->flags = BAST_CELL_SOLID;
        bast_map_cell_at(m, x, 3)->flags = BAST_CELL_SOLID;
    }
    add_entity(m, BAST_ENT_PLAYER_START, 1, 2, 0, 0);
    add_entity(m, BAST_ENT_KIBBLE_BARREL, 5, 2, 0, 0);
    add_entity(m, BAST_ENT_KIBBLE_BARREL, 3, 2, 0, 0);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    nav_state ns;
    nav_reset(&ns);
    ticcmd cmd;
    assert(nav_goto(&gs, 9, 2, &ns, &cmd) == NAV_MOVING);
    assert(cmd.fwd > 0 || cmd.turn != 0);
    nav_reset(&ns);
    assert(nav_goto(&gs, 3, 2, &ns, &cmd) == NAV_MOVING); /* goal holds a barrel */
    /* Pushing into the barrel at (3, 2) for long: still MOVING every tic
     * (the demo's max times it out), the stall never turns into NO_PATH. */
    nav_reset(&ns);
    for (int t = 0; t < 200; t++) {
        assert(nav_goto(&gs, 9, 2, &ns, &cmd) == NAV_MOVING);
        game_tic(&gs, &cmd);
    }
    assert(gs.player.x <= FX(3)); /* stopped at the barrel at (3, 2) */
    bast_map_free(m);
}

/* A Dachshund (radius 0.5) whose centre sits in tile (6, 5) just below its
 * north edge, so its circle also covers the centre line of row 6, where
 * the player walks east. Its centre is not in row 6, so the first plan
 * goes straight into it; the stall rule then avoids tile (6, 6) and the
 * player gets around through row 7. */
static void test_stall(void)
{
    bast_map *m = open_map(14, 9);
    add_entity(m, BAST_ENT_PLAYER_START, 2, 6, 0, 0);
    int i = bast_map_add_entity(m);
    m->entities[i].type = BAST_ENT_DACHSHUND_HOWITZER;
    m->entities[i].x = CENTRE(6);
    m->entities[i].y = FX(6) - FIX_ONE / 256; /* tile (6, 5) */
    m->entities[i].angle = 0;
    m->entities[i].flags = BAST_ENT_SKILL_ALL | BAST_ENT_AMBUSH;
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    int t = run_goto(&gs, 11, 6, 600, -1, -1);
    printf("stall: arrived after %d tics, stalled=%d\n", t, stalled);
    assert(t > 0 && t < 300);
    assert(stalled); /* the plan alone walks into it: the stall rule got it through */
    bast_map_free(m);
}

int main(int argc, char **argv)
{
    const char *g = argc > 1 ? argv[1] : "";
    if (!strcmp(g, "around"))
        test_around();
    else if (!strcmp(g, "fallback"))
        test_fallback();
    else if (!strcmp(g, "stall"))
        test_stall();
    else {
        fprintf(stderr, "usage: test_demo_nav around|fallback|stall\n");
        return 2;
    }
    printf("demo_nav %s: OK\n", g);
    return 0;
}
