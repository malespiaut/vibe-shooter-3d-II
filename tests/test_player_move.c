/* Player movement and tile collision (tech.md World: Player), plus the
 * state hash. Maps are built in memory with libbast. */
#include <assert.h>
#include <stdio.h>

#include "bast.h"
#include "game/state.h"

#define FX(i) fix_from_int(i)
#define HALF (FIX_ONE / 2)

/* 16x16 map, solid border, everything else open. */
static bast_map *open_map(void)
{
    bast_map *m = bast_map_new(16, 16);
    assert(m);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            if (x == 0 || y == 0 || x == 15 || y == 15)
                bast_map_cell_at(m, x, y)->flags = BAST_CELL_SOLID;
    return m;
}

static void solid(bast_map *m, int x, int y, uint16_t flags)
{
    bast_map_cell_at(m, x, y)->flags = flags;
}

static void place(game_state *gs, const bast_map *m, fix_t x, fix_t y, angle_t a)
{
    game_init(gs, m, 1, 2, 0);
    gs->player.x = x;
    gs->player.y = y;
    gs->player.angle = a;
}

static const ticcmd WALK = { 127, 0, 0, 0 };
static const ticcmd IDLE = { 0, 0, 0, 0 };

static void run(game_state *gs, const ticcmd *cmd, int n)
{
    for (int i = 0; i < n; i++) {
        game_tic(gs, cmd);
        /* Invariant: never inside a blocking tile, never overlapping one. */
        assert(!game_tile_blocks(gs, fix_to_int(gs->player.x), fix_to_int(gs->player.y)));
        assert(!game_circle_blocked(gs, gs->player.x, gs->player.y, PLAYER_RADIUS));
    }
}

static void test_walk_distance(void)
{
    bast_map *m = open_map();
    game_state gs;
    place(&gs, m, FX(2) + HALF, FX(8) + HALF, 0);
    const fix_t x0 = gs.player.x;

    /* Accelerates in 4 tics: 1873, 3746, 5619, then capped at 7490.
     * 35 tics: 1873 + 3746 + 5619 + 32 * 7490 = 250918. */
    run(&gs, &WALK, 4);
    assert(gs.player.momx == PLAYER_WALK_SPEED);
    run(&gs, &WALK, 31);
    fix_t d = gs.player.x - x0;
    printf("walk 35 tics: %d (expect 250918)\n", d);
    assert(d >= 250918 - 1 && d <= 250918 + 1);
    assert(gs.player.y == FX(8) + HALF);
    assert(gs.tic == 35);

    /* Friction stops within 4 tics, from walk and from run. */
    run(&gs, &IDLE, 4);
    assert(gs.player.momx == 0 && gs.player.momy == 0);
    place(&gs, m, FX(2) + HALF, FX(8) + HALF, 0);
    const ticcmd runcmd = { 127, 0, 0, BT_RUN };
    run(&gs, &runcmd, 4);
    assert(gs.player.momx == PLAYER_RUN_SPEED);
    run(&gs, &IDLE, 4);
    assert(gs.player.momx == 0);

    /* Keyboard turn: 1872 units per tic. */
    const ticcmd turn = { 0, 0, PLAYER_TURN_SPEED, 0 };
    run(&gs, &turn, 35);
    assert(gs.player.angle == (angle_t)(35 * PLAYER_TURN_SPEED));
    bast_map_free(m);
}

static void test_wall_stop(void)
{
    bast_map *m = open_map();
    for (int y = 1; y < 15; y++)
        solid(m, 10, y, BAST_CELL_SOLID);
    game_state gs;
    place(&gs, m, FX(5) + HALF, FX(8) + HALF, 0);
    run(&gs, &WALK, 100);
    printf("wall stop: x=%d (expect %d)\n", gs.player.x, FX(10) - PLAYER_RADIUS);
    assert(gs.player.x == FX(10) - PLAYER_RADIUS);
    assert(gs.player.momx == 0);

    /* BLOCK_ALL (water) stops the player the same way, from the west. */
    bast_map *w = open_map();
    for (int y = 1; y < 15; y++)
        solid(w, 4, y, BAST_CELL_BLOCK_ALL);
    place(&gs, w, FX(8) + HALF, FX(8) + HALF, ANG180);
    run(&gs, &WALK, 100);
    assert(gs.player.x == FX(5) + PLAYER_RADIUS);
    bast_map_free(w);
    bast_map_free(m);
}

static void test_slide_45(void)
{
    bast_map *m = open_map();
    for (int y = 1; y < 15; y++)
        solid(m, 10, y, BAST_CELL_SOLID);
    game_state gs;
    place(&gs, m, FX(9) + HALF, FX(2) + HALF, ANG45);
    run(&gs, &WALK, 20);
    assert(gs.player.x == FX(10) - PLAYER_RADIUS);

    /* Against the wall, the y (parallel) component of the walk velocity is
     * kept in full every tic. */
    const fix_t parallel = fix_mul(PLAYER_WALK_SPEED, fine_sin(ANG45));
    for (int i = 0; i < 10; i++) {
        fix_t y0 = gs.player.y;
        run(&gs, &WALK, 1);
        printf("%sslide dy=%d", i ? ", " : "", gs.player.y - y0);
        assert(gs.player.y - y0 == parallel);
        assert(gs.player.x == FX(10) - PLAYER_RADIUS);
    }
    printf(" (expect %d)\n", parallel);
    bast_map_free(m);
}

static void test_gap(void)
{
    bast_map *m = open_map();
    for (int y = 1; y < 15; y++)
        if (y != 8)
            solid(m, 10, y, BAST_CELL_SOLID);
    game_state gs;
    place(&gs, m, FX(5) + HALF, FX(8) + HALF, 0);
    run(&gs, &WALK, 100);
    printf("gap: x=%d\n", gs.player.x);
    assert(gs.player.x > FX(12));
    assert(gs.player.y == FX(8) + HALF);
    bast_map_free(m);
}

static void test_diagonal_pinch(void)
{
    /* Tiles (10,8) and (11,9) touch only at the corner point (11,9). Walk at
     * 45 degrees straight at that point: the circle must not squeeze through,
     * and the centre must never enter a solid tile (checked in run()). */
    bast_map *m = open_map();
    solid(m, 10, 8, BAST_CELL_SOLID);
    solid(m, 11, 9, BAST_CELL_SOLID);
    game_state gs;
    /* Start on the line x - y = 2 through (11,9), south-west of the pinch. */
    place(&gs, m, FX(8), FX(6), ANG45);
    run(&gs, &WALK, 200);
    printf("pinch: x=%d y=%d\n", gs.player.x, gs.player.y);
    assert(!(gs.player.x > FX(11) && gs.player.y > FX(9)));

    /* The same with run speed from the other side. */
    place(&gs, m, FX(14), FX(12), (angle_t)(ANG180 + ANG45));
    const ticcmd runcmd = { 127, 0, 0, BT_RUN };
    run(&gs, &runcmd, 200);
    assert(!(gs.player.x < FX(11) && gs.player.y < FX(9)));
    bast_map_free(m);
}

static void test_hash(void)
{
    bast_map *m = open_map();
    game_state a, b;
    game_init(&a, m, 1, 2, 0);
    game_init(&b, m, 1, 2, 0);
    assert(game_hash(&a) == game_hash(&b));

    uint64_t h0 = game_hash(&a);
    game_tic(&a, &WALK);
    assert(game_hash(&a) != h0);

    /* Same ticcmd sequence -> same hash. */
    game_tic(&b, &WALK);
    for (int i = 0; i < 300; i++) {
        ticcmd c = { (int8_t)((i * 37) % 255 - 127), (int8_t)((i * 11) % 255 - 127),
                     (int16_t)((i % 7) * 500 - 1500), (uint8_t)((i & 8) ? BT_RUN : 0) };
        game_tic(&a, &c);
        game_tic(&b, &c);
    }
    assert(game_hash(&a) == game_hash(&b));
    printf("hash after 301 tics: %016llx\n", (unsigned long long)game_hash(&a));

    /* A different level seed changes the hash (RNG state is hashed). */
    game_init(&b, m, 2, 2, 0);
    game_init(&a, m, 1, 2, 0);
    assert(game_hash(&a) != game_hash(&b));
    bast_map_free(m);
}

int main(void)
{
    test_walk_distance();
    test_wall_stop();
    test_slide_45();
    test_gap();
    test_diagonal_pinch();
    test_hash();
    puts("player_move: ok");
    return 0;
}
