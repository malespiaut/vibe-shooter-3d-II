/* Doors and keys (tech.md Map: DOOR, ONEWAY, START_OPEN, door param;
 * items.md Keys) on maps/doortest.vmap (tools/mkmap.py), plus a replay of
 * demos/door-test.demo.
 *   test_door <maps/doortest.vmap> <demos/door-test.demo> */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "bast.h"
#include "game/demo.h"
#include "game/state.h"

#define FX(i) fix_from_int(i)
#define HALF (FIX_ONE / 2)

static const bast_map *map;

static const ticcmd IDLE = { 0, 0, 0, 0 };
static const ticcmd USE = { 0, 0, 0, BT_USE };
static const ticcmd WALK = { 127, 0, 0, 0 };

static void run(game_state *gs, const ticcmd *cmd, int n)
{
    for (int i = 0; i < n; i++) {
        game_tic(gs, cmd);
        /* Invariant: the player never overlaps a blocking tile. */
        assert(!game_circle_blocked(gs, gs->player.x, gs->player.y, PLAYER_RADIUS));
    }
}

/* Fresh state, player at (x, y) in 16.16, facing `a`. */
static void place(game_state *gs, fix_t x, fix_t y, angle_t a)
{
    game_init(gs, map, 1, 0);
    gs->player.x = x;
    gs->player.y = y;
    gs->player.angle = a;
}

/* Player just south of door column tx (y = 3.75), facing north. */
static void below(game_state *gs, int tx)
{
    place(gs, FX(tx) + HALF, FX(3) + FIX_ONE * 3 / 4, ANG90);
}

static void above(game_state *gs, int tx)
{
    place(gs, FX(tx) + HALF, FX(5) + FIX_ONE / 4, ANG270);
}

static void use_once(game_state *gs)
{
    run(gs, &USE, 1);
    run(gs, &IDLE, 1); /* release: the next press is a new rising edge */
}

static void test_init(void)
{
    game_state gs;
    game_init(&gs, map, 1, 0);
    assert(gs.door_count == 5);
    /* Row-major: all on row 4, x = 2, 4, 6, 8, 10. */
    for (int i = 0; i < 5; i++) {
        assert(gs.doors[i].ty == 4 && gs.doors[i].tx == 2 + 2 * i);
        assert(door_at(&gs, 2 + 2 * i, 4) == i);
    }
    assert(door_at(&gs, 3, 4) == -1);
    for (int i = 0; i < 4; i++)
        assert(gs.doors[i].state == DOOR_CLOSED && game_tile_blocks(&gs, 2 + 2 * i, 4));
}

static void test_plain_opens_and_closes(void)
{
    game_state gs;
    below(&gs, 2);
    assert(game_tile_blocks(&gs, 2, 4));
    run(&gs, &USE, 1);
    assert(gs.doors[0].state == DOOR_OPENING);
    run(&gs, &USE, 30); /* held: no new rising edge, no effect */
    assert(game_tile_blocks(&gs, 2, 4)); /* 31 tics: not yet */
    run(&gs, &IDLE, 1);
    assert(!game_tile_blocks(&gs, 2, 4)); /* 32 tics: fully open */
    assert(gs.doors[0].state == DOOR_OPEN && gs.doors[0].pos == FIX_ONE);

    run(&gs, &IDLE, 139);
    assert(gs.doors[0].state == DOOR_OPEN);
    run(&gs, &IDLE, 1); /* 140 tics after fully open: starts closing */
    assert(gs.doors[0].state == DOOR_CLOSING);
    assert(game_tile_blocks(&gs, 2, 4));
    run(&gs, &IDLE, 30);
    assert(gs.doors[0].state == DOOR_CLOSING);
    run(&gs, &IDLE, 1); /* 32 tics of closing, counting the first */
    assert(gs.doors[0].state == DOOR_CLOSED && gs.doors[0].pos == 0);
}

static void test_does_not_close_on_player(void)
{
    game_state gs;
    below(&gs, 2);
    use_once(&gs);
    run(&gs, &IDLE, 30);
    assert(!game_tile_blocks(&gs, 2, 4));
    /* Step into the doorway and stay there well past the close time. */
    gs.player.y = FX(4) + HALF;
    run(&gs, &IDLE, 400);
    assert(gs.doors[0].state == DOOR_OPEN && !game_tile_blocks(&gs, 2, 4));

    /* Leave north: the door closes once the cell is clear. */
    gs.player.y = FX(6) + HALF;
    run(&gs, &IDLE, 1);
    assert(gs.doors[0].state == DOOR_CLOSING);

    /* Closing door re-opens when the player is in its cell. */
    gs.player.y = FX(5) + HALF / 4; /* radius overlaps row 4 */
    gs.doors[0].pos = FIX_ONE / 2;
    assert(game_circle_overlaps_tile(gs.player.x, gs.player.y, PLAYER_RADIUS, 2, 4));
    game_tic(&gs, &IDLE);
    assert(gs.doors[0].state == DOOR_OPENING);
}

static void test_keyed(void)
{
    game_state gs;
    below(&gs, 4);
    use_once(&gs);
    run(&gs, &IDLE, 60);
    assert(gs.doors[1].state == DOOR_CLOSED && game_tile_blocks(&gs, 4, 4));
    /* Wrong key: still shut. */
    game_give_keys(&gs, KEY_BLUE | KEY_VIOLET);
    use_once(&gs);
    run(&gs, &IDLE, 60);
    assert(gs.doors[1].state == DOOR_CLOSED);
    game_give_keys(&gs, KEY_GREEN);
    assert(gs.player.keys == (KEY_GREEN | KEY_BLUE | KEY_VIOLET));
    use_once(&gs);
    run(&gs, &IDLE, 30);
    assert(!game_tile_blocks(&gs, 4, 4));
}

static void test_oneway(void)
{
    game_state gs;
    above(&gs, 6); /* north side: refused */
    use_once(&gs);
    run(&gs, &IDLE, 60);
    assert(gs.doors[2].state == DOOR_CLOSED);
    below(&gs, 6); /* south side: opens */
    use_once(&gs);
    run(&gs, &IDLE, 30);
    assert(!game_tile_blocks(&gs, 6, 4));
}

static void test_stay_open(void)
{
    game_state gs;
    above(&gs, 8); /* mode stay, usable from either side */
    use_once(&gs);
    run(&gs, &IDLE, 30);
    assert(!game_tile_blocks(&gs, 8, 4));
    run(&gs, &IDLE, 1000);
    assert(gs.doors[3].state == DOOR_OPEN && !game_tile_blocks(&gs, 8, 4));
}

static void test_start_open(void)
{
    game_state gs;
    below(&gs, 10);
    assert(gs.doors[4].state == DOOR_OPEN && !game_tile_blocks(&gs, 10, 4));
    run(&gs, &IDLE, 1000); /* never auto-closes */
    assert(!game_tile_blocks(&gs, 10, 4));
    use_once(&gs);          /* use does not close it */
    assert(!game_tile_blocks(&gs, 10, 4));
    /* A tag closes it. */
    door_activate(&gs, 4);
    run(&gs, &IDLE, 32);
    assert(gs.doors[4].state == DOOR_CLOSED && game_tile_blocks(&gs, 10, 4));
    /* Player walks through it while open: never inside a blocking tile
     * (run() checks) and ends in the north hall. */
    below(&gs, 10);
    run(&gs, &WALK, 25);
    assert(gs.player.y > FX(5));
}

static void test_use_range_and_switch_mode(void)
{
    game_state gs;
    /* 1 unit away (y = 3.0 is 1.0 from the door edge): out of range. */
    place(&gs, FX(2) + HALF, FX(3), ANG90);
    use_once(&gs);
    assert(gs.doors[0].state == DOOR_CLOSED);
    /* Facing away: nothing. */
    place(&gs, FX(2) + HALF, FX(3) + FIX_ONE * 3 / 4, ANG270);
    use_once(&gs);
    assert(gs.doors[0].state == DOOR_CLOSED);
    /* Diagonal within range: 45 degrees from (2.4, 3.6) reaches row 4. */
    place(&gs, FX(2) + FIX_ONE * 2 / 5, FX(3) + FIX_ONE * 3 / 5, ANG45);
    assert(door_use_target(&gs) == 0);
    /* Switch-only door ignores use, opens on activation. */
    below(&gs, 2);
    gs.doors[0].param = 2 << 2;
    use_once(&gs);
    assert(gs.doors[0].state == DOOR_CLOSED);
    door_activate(&gs, 0);
    run(&gs, &IDLE, 32);
    assert(!game_tile_blocks(&gs, 2, 4));
}

static void test_hash(void)
{
    game_state a, b;
    below(&a, 2);
    below(&b, 2);
    assert(game_hash(&a) == game_hash(&b));
    run(&a, &USE, 1);
    run(&b, &IDLE, 1);
    assert(game_hash(&a) != game_hash(&b)); /* door state differs */
    below(&a, 2);
    below(&b, 2);
    game_give_keys(&b, KEY_GREEN);
    assert(game_hash(&a) != game_hash(&b)); /* keys are hashed */
}

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    assert(buf && fread(buf, 1, (size_t)n, f) == (size_t)n);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void test_demo(const char *path)
{
    char err[256];
    char *text = slurp(path);
    demo_t *d = demo_parse(text, err, sizeof err);
    assert(d);
    game_state gs;
    game_init(&gs, map, d->level, d->seed);
    ticcmd cmd;
    int r;
    while ((r = demo_next(d, &gs, &cmd, err, sizeof err)) == DEMO_TIC)
        run(&gs, &cmd, 1);
    assert(r == DEMO_END);
    printf("door-test.demo: tic=%u player=(%d/65536, %d/65536)\n", (unsigned)gs.tic,
           (int)gs.player.x, (int)gs.player.y);
    assert(gs.player.y > FX(5)); /* past the door at y = 4 */
    assert(gs.doors[0].state != DOOR_CLOSED);
    demo_free(d);
    free(text);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    bast_map *m = bast_map_load(argv[1]);
    assert(m);
    map = m;
    test_init();
    test_plain_opens_and_closes();
    test_does_not_close_on_player();
    test_keyed();
    test_oneway();
    test_stay_open();
    test_start_open();
    test_use_range_and_switch_mode();
    test_hash();
    test_demo(argv[2]);
    bast_map_free(m);
    puts("door: ok");
    return 0;
}
