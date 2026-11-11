/* Map logic (tech.md Map: cell flags and special entities): tags, switches,
 * exit, pushwalls, triggers, secrets, lights, damage floors, on
 * maps/tagstest.vmap (tools/mkmap.py, layout in maps/src/tagstest.txt).
 *   test_tags <maps/tagstest.vmap> <case>
 * Each case is its own ctest (tags_<case>). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bast.h"
#include "game/state.h"

#define FX(i) fix_from_int(i)
#define HALF (FIX_ONE / 2)

static const bast_map *map;

static const ticcmd IDLE = { 0, 0, 0, 0 };
static const ticcmd USE = { 0, 0, 0, BT_USE };

static void run(game_state *gs, const ticcmd *cmd, int n)
{
    for (int i = 0; i < n; i++) {
        game_tic(gs, cmd);
        assert(!game_circle_blocked(gs, gs->player.x, gs->player.y, PLAYER_RADIUS));
    }
}

/* Fresh state, player at the centre of tile (tx, ty), facing `a`. */
static void place(game_state *gs, int tx, int ty, angle_t a)
{
    game_init(gs, map, 1, 2, 0);
    gs->player.x = FX(tx) + HALF;
    gs->player.y = FX(ty) + HALF;
    gs->player.angle = a;
}

static void use_once(game_state *gs)
{
    run(gs, &USE, 1);
    run(gs, &IDLE, 1);
}

static const door_t *door(const game_state *gs, int tx, int ty)
{
    int d = door_at(gs, tx, ty);
    assert(d >= 0);
    return &gs->doors[d];
}

/* s (0,2) fires tag 1 once: door 1 (6,4) opens and stays open. */
static void test_switch(void)
{
    game_state gs;
    place(&gs, 1, 2, ANG180);
    assert(gs.cell_fired[2 * map->width + 0] == 0);
    assert(game_tile_blocks(&gs, 6, 4));
    use_once(&gs);
    assert(gs.cell_fired[2 * map->width + 0] & FIRED_SWITCH);
    assert(gs.tags_fired == 1);
    assert(door(&gs, 6, 4)->state == DOOR_OPENING);
    run(&gs, &IDLE, 31);
    assert(!game_tile_blocks(&gs, 6, 4));
    run(&gs, &IDLE, 400);
    assert(door(&gs, 6, 4)->state == DOOR_OPEN); /* mode switch: stays */
    use_once(&gs); /* once only */
    assert(gs.tags_fired == 1);
    /* Plain use on a switch-mode door does nothing. */
    game_state g2;
    place(&g2, 6, 3, ANG90);
    use_once(&g2);
    assert(door(&g2, 6, 4)->state == DOOR_CLOSED);
    printf("tags_switch: ok\n");
}

/* The START_OPEN door o (10,4) closes on the same tag and stays closed. */
static void test_start_open(void)
{
    game_state gs;
    place(&gs, 1, 2, ANG180);
    assert(door(&gs, 10, 4)->state == DOOR_OPEN && !game_tile_blocks(&gs, 10, 4));
    run(&gs, &IDLE, 300); /* START_OPEN never auto-closes */
    assert(door(&gs, 10, 4)->state == DOOR_OPEN);
    run(&gs, &USE, 1);
    assert(door(&gs, 10, 4)->state == DOOR_CLOSING);
    assert(game_tile_blocks(&gs, 10, 4)); /* blocks as soon as it moves */
    run(&gs, &IDLE, 31);
    assert(door(&gs, 10, 4)->state == DOOR_CLOSED && door(&gs, 10, 4)->pos == 0);
    run(&gs, &IDLE, 400);
    assert(door(&gs, 10, 4)->state == DOOR_CLOSED);
    printf("tags_start_open: ok\n");
}

/* w (0,1), param 3: timed door T (3,4) opens, starts closing 3 s = 105 tics
 * after the switch fired, closed 32 tics later. */
static void test_timed(void)
{
    game_state gs;
    place(&gs, 1, 1, ANG180);
    run(&gs, &USE, 1); /* fired on tic 1 (counts as the first delay tic) */
    assert(door(&gs, 3, 4)->state == DOOR_OPENING);
    run(&gs, &IDLE, 103); /* 104 tics since the switch */
    assert(door(&gs, 3, 4)->state == DOOR_OPEN);
    run(&gs, &IDLE, 1); /* 105 */
    assert(door(&gs, 3, 4)->state == DOOR_CLOSING);
    run(&gs, &IDLE, 31);
    assert(door(&gs, 3, 4)->state == DOOR_CLOSED);
    run(&gs, &IDLE, 200);
    assert(door(&gs, 3, 4)->state == DOOR_CLOSED);

    /* Never closes on the player: standing in it delays the close. */
    place(&gs, 1, 1, ANG180);
    run(&gs, &USE, 1);
    run(&gs, &IDLE, 40);
    gs.player.x = FX(3) + HALF;
    gs.player.y = FX(4) + HALF;
    run(&gs, &IDLE, 200);
    assert(door(&gs, 3, 4)->state == DOOR_OPEN);
    gs.player.y = FX(3) + HALF;
    run(&gs, &IDLE, 1);
    assert(door(&gs, 3, 4)->state == DOOR_CLOSING);
    printf("tags_timed: ok\n");
}

/* P (3,8) used from (3,7) facing north: slides 2 tiles at 1 per 32 tics. */
static void test_pushwall(void)
{
    game_state gs;
    place(&gs, 3, 7, ANG90);
    int pw = pushwall_at(&gs, 3, 8);
    assert(pw >= 0 && gs.pushwall_count == 2);
    assert(game_tile_blocks(&gs, 3, 8) && !game_tile_blocks(&gs, 3, 9));
    run(&gs, &USE, 1);
    assert(gs.pushwalls[pw].state == PUSHWALL_MOVING);
    assert(gs.pushwalls[pw].dx == 0 && gs.pushwalls[pw].dy == 1);
    assert(game_tile_blocks(&gs, 3, 8) && game_tile_blocks(&gs, 3, 9));
    run(&gs, &IDLE, 31); /* 32 tics: one tile */
    assert(gs.pushwalls[pw].ty == 9 && gs.pushwalls[pw].state == PUSHWALL_MOVING);
    assert(!game_tile_blocks(&gs, 3, 8));
    run(&gs, &IDLE, 31);
    assert(gs.pushwalls[pw].ty == 9);
    run(&gs, &IDLE, 1); /* 64 tics: two tiles, done */
    assert(gs.pushwalls[pw].ty == 10 && gs.pushwalls[pw].tx == 3);
    assert(gs.pushwalls[pw].state == PUSHWALL_DONE);
    assert(!game_tile_blocks(&gs, 3, 8) && !game_tile_blocks(&gs, 3, 9));
    assert(game_tile_blocks(&gs, 3, 10)); /* solid at its destination */
    run(&gs, &IDLE, 100);
    assert(gs.pushwalls[pw].ty == 10);
    use_once(&gs); /* nothing more to push from here (out of range) */
    assert(gs.pushwalls[pw].ty == 10 && gs.pushwalls[pw].state == PUSHWALL_DONE);

    /* Q (7,8): (7,10) is wall, so it stops after 1 tile at (7,9). */
    place(&gs, 7, 7, ANG90);
    int q = pushwall_at(&gs, 7, 8);
    assert(q >= 0 && q != pw);
    run(&gs, &USE, 1);
    run(&gs, &IDLE, 200);
    assert(gs.pushwalls[q].tx == 7 && gs.pushwalls[q].ty == 9);
    assert(gs.pushwalls[q].state == PUSHWALL_DONE && gs.pushwalls[q].moved == 1);
    assert(!game_tile_blocks(&gs, 7, 8) && game_tile_blocks(&gs, 7, 9));

    /* P again, with the player standing in (3,10): it never slides into
     * the player, so it stops after 1 tile at (3,9). */
    place(&gs, 3, 7, ANG90);
    run(&gs, &USE, 1);
    assert(gs.pushwalls[pw].state == PUSHWALL_MOVING);
    gs.player.y = FX(10) + HALF;
    run(&gs, &IDLE, 200);
    assert(gs.pushwalls[pw].ty == 9 && gs.pushwalls[pw].state == PUSHWALL_DONE);
    assert(gs.pushwalls[pw].moved == 1 && !game_tile_blocks(&gs, 3, 10));
    printf("tags_pushwall: ok\n");
}

/* Walking onto r (9,6) fires tag 3 once: door 3 (8,4) opens, lights fire. */
static void test_trigger(void)
{
    game_state gs;
    place(&gs, 9, 5, ANG90);
    run(&gs, &IDLE, 1);
    assert(gs.tags_fired == 0 && door(&gs, 8, 4)->state == DOOR_CLOSED);
    const ticcmd walk = { 127, 0, 0, 0 };
    int n = 0;
    while (fix_to_int(gs.player.y) != 6) {
        run(&gs, &walk, 1);
        assert(++n < 35);
    }
    assert(gs.tags_fired == 1 && door(&gs, 8, 4)->state == DOOR_OPENING);
    assert(gs.lights_alt == 1);
    /* Leave and re-enter: no second fire. */
    gs.player.y = FX(5) + HALF;
    run(&gs, &IDLE, 1);
    gs.player.y = FX(6) + HALF;
    run(&gs, &IDLE, 5);
    assert(gs.tags_fired == 1);
    printf("tags_trigger: ok\n");
}

/* Entering ! (3,9) counts one secret of one, once. */
static void test_secret(void)
{
    game_state gs;
    place(&gs, 3, 7, ANG90);
    assert(gs.secret_total == 1 && gs.secrets == 0);
    run(&gs, &USE, 1);
    run(&gs, &IDLE, 70); /* pushwall P out of the way */
    const ticcmd walk = { 127, 0, 0, 0 };
    int n = 0;
    while (fix_to_int(gs.player.y) != 9) {
        run(&gs, &walk, 1);
        assert(++n < 70);
    }
    assert(gs.secrets == 1);
    run(&gs, &walk, 20); /* bumps into the pushwall at (3,10), stays */
    const ticcmd back = { -127, 0, 0, 0 };
    while (fix_to_int(gs.player.y) != 7)
        run(&gs, &back, 1);
    run(&gs, &IDLE, 6);
    while (fix_to_int(gs.player.y) != 9)
        run(&gs, &walk, 1);
    assert(gs.secrets == 1);
    printf("tags_secret: ok\n");
}

/* Tag 3 fires the LIGHTS entity: every cell reads dark_alt. */
static void test_lights(void)
{
    game_state gs;
    place(&gs, 2, 2, 0);
    assert(game_cell_dark(&gs, 5, 5) == 0);
    game_fire_tag(&gs, 1); /* another tag: no lights */
    assert(!gs.lights_alt && game_cell_dark(&gs, 5, 5) == 0);
    uint64_t before = game_hash(&gs);
    game_fire_tag(&gs, 3);
    assert(gs.lights_alt == 1);
    assert(game_hash(&gs) != before); /* hashed */
    for (int y = 0; y < map->height; y++)
        for (int x = 0; x < map->width; x++)
            assert(game_cell_dark(&gs, x, y) == map->cells[y * map->width + x].dark_alt);
    assert(game_cell_dark(&gs, 5, 5) == 12);
    printf("tags_lights: ok\n");
}

/* ~ (11,7), param 5: 5 health on every 16th tic while standing in it. */
static void test_damage(void)
{
    game_state gs;
    place(&gs, 11, 7, 0);
    int32_t last = gs.player.health;
    int hits = 0;
    uint32_t prev_hit = 0;
    for (int i = 0; i < 64; i++) {
        run(&gs, &IDLE, 1);
        if (gs.player.health != last) {
            assert(last - gs.player.health == 5);
            assert(gs.tic % DAMAGE_PERIOD == 1); /* game_tic checked tic-1 */
            if (hits)
                assert(gs.tic - prev_hit == DAMAGE_PERIOD);
            prev_hit = gs.tic;
            hits++;
            last = gs.player.health;
        }
    }
    assert(hits == 4 && gs.player.health == PLAYER_START_HEALTH - 20);
    /* Off the cell: no damage. */
    gs.player.x = FX(10) + HALF;
    run(&gs, &IDLE, 64);
    assert(gs.player.health == PLAYER_START_HEALTH - 20);
    printf("tags_damage: ok\n");
}

/* E (1,8) used from (1,7) facing north: exit = 1. */
static void test_exit(void)
{
    game_state gs;
    place(&gs, 1, 7, ANG90);
    assert(gs.exit == 0);
    uint64_t before = game_hash(&gs);
    run(&gs, &USE, 1);
    assert(gs.exit == 1);
    game_state g2;
    place(&g2, 1, 7, ANG90);
    run(&g2, &IDLE, 1); /* same tic count, no use */
    assert(game_hash(&g2) != game_hash(&gs) && before != game_hash(&gs));
    /* Facing away: nothing. */
    place(&gs, 1, 7, ANG270);
    run(&gs, &USE, 1);
    assert(gs.exit == 0);
    printf("tags_exit: ok\n");
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: test_tags <tagstest.vmap> <case>\n");
        return 2;
    }
    static const struct {
        const char *name;
        void (*fn)(void);
    } cases[] = {
        { "switch", test_switch },   { "start_open", test_start_open },
        { "timed", test_timed },     { "pushwall", test_pushwall },
        { "trigger", test_trigger }, { "secret", test_secret },
        { "lights", test_lights },   { "damage", test_damage },
        { "exit", test_exit },
    };
    bast_map *m = bast_map_load(argv[1]);
    if (!m) {
        fprintf(stderr, "test_tags: %s: %s\n", argv[1], bast_error());
        return 1;
    }
    map = m;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        if (strcmp(argv[2], cases[i].name) == 0) {
            cases[i].fn();
            bast_map_free(m);
            return 0;
        }
    }
    fprintf(stderr, "test_tags: unknown case '%s'\n", argv[2]);
    return 2;
}
