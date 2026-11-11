/* Enemy AI (enemies.md AI (shared), Stats; tech.md Skill). Maps are built
 * in memory with libbast. Usage: test_ai <group>. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "game/ai.h"
#include "game/enemy_info.h"
#include "game/items.h"
#include "game/state.h"
#include "game/tags.h"

#define FX(i) fix_from_int(i)
#define HALF (FIX_ONE / 2)
#define CENTRE(i) (FX(i) + HALF)

static const ticcmd IDLE = { 0, 0, 0, 0 };
static const ticcmd FIRE = { 0, 0, 0, BT_FIRE };

/* w x h, solid border, open inside. */
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
    m->entities[i].flags = flags;
}

static void run(game_state *gs, const ticcmd *cmd, int n)
{
    for (int i = 0; i < n; i++)
        game_tic(gs, cmd);
}

/* Player start at tile (tx, ty) facing `a`. */
static void add_player(bast_map *m, int tx, int ty, angle_t a)
{
    add_entity(m, BAST_ENT_PLAYER_START, tx, ty, a, BAST_ENT_SKILL_ALL);
}

/* Idle Mutt facing west wakes when the player in front is visible, not
 * when a wall is between them, and not when the player is behind it. */
static void test_sight(void)
{
    bast_map *m = open_map(16, 8);
    add_player(m, 2, 3, 0);
    add_entity(m, BAST_ENT_MUTT_TROOPER, 10, 3, ANG180, BAST_ENT_SKILL_ALL); /* in view */
    add_entity(m, BAST_ENT_MUTT_TROOPER, 10, 5, 0, BAST_ENT_SKILL_ALL);      /* faces away */
    add_entity(m, BAST_ENT_MUTT_TROOPER, 10, 1, ANG180, BAST_ENT_SKILL_ALL); /* behind a wall */
    for (int x = 4; x <= 8; x++)
        bast_map_cell_at(m, x, 2)->flags = BAST_CELL_SOLID; /* wall row y=2 */
    bast_map_cell_at(m, 9, 2)->flags = BAST_CELL_SOLID;
    bast_map_cell_at(m, 3, 2)->flags = BAST_CELL_SOLID;
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    gs.player.y = CENTRE(3);
    /* Wall between the player (2.5, 3.5) and the Mutt at (10.5, 1.5)?
     * The line crosses y = 3 .. 2 between x 2.5 and 10.5, i.e. row y=2
     * at x 6.5..10.5: tiles 6..9 are solid. */
    game_tic(&gs, &IDLE);
    printf("sight: in view %d, facing away %d, behind wall %d\n", gs.things.list[0].state,
           gs.things.list[1].state, gs.things.list[2].state);
    assert(gs.things.list[0].state != TH_STATE_STAND);
    assert(gs.things.list[1].state == TH_STATE_STAND);
    assert(gs.things.list[2].state == TH_STATE_STAND);
    assert(!ai_line_of_sight(&gs, &gs.things.list[2]));
    run(&gs, &IDLE, 35);
    assert(gs.things.list[1].state == TH_STATE_STAND && gs.things.list[2].state == TH_STATE_STAND);
    bast_map_free(m);
}

/* A gunshot wakes a normal Mutt facing away but not an AMBUSH one; the
 * ambusher wakes once it sees the player. Noise does not cross a closed
 * door. */
static void test_ambush(void)
{
    bast_map *m = open_map(20, 8);
    add_player(m, 2, 3, ANG90); /* faces north: shoots the wall */
    add_entity(m, BAST_ENT_MUTT_TROOPER, 10, 3, 0, BAST_ENT_SKILL_ALL); /* faces away */
    add_entity(m, BAST_ENT_MUTT_TROOPER, 10, 5, 0, BAST_ENT_SKILL_ALL | BAST_ENT_AMBUSH);
    add_entity(m, BAST_ENT_MUTT_TROOPER, 17, 3, 0, BAST_ENT_SKILL_ALL); /* past a door */
    for (int y = 1; y < 7; y++)
        bast_map_cell_at(m, 15, y)->flags = BAST_CELL_SOLID;
    bast_map_cell_at(m, 15, 3)->flags = BAST_CELL_DOOR;
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    game_tic(&gs, &FIRE);
    assert(gs.player.weapons.shots == 1);
    printf("ambush: plain %d, ambush %d, behind door %d\n", gs.things.list[0].state,
           gs.things.list[1].state, gs.things.list[2].state);
    assert(gs.things.list[0].state != TH_STATE_STAND);
    assert(gs.things.list[1].state == TH_STATE_STAND);
    assert(gs.things.list[2].state == TH_STATE_STAND);
    run(&gs, &IDLE, 20);
    assert(gs.things.list[1].state == TH_STATE_STAND);
    /* Turn the ambusher toward the player: it sees and wakes. */
    gs.things.list[1].angle = angle_of(gs.player.x - gs.things.list[1].x,
                                       gs.player.y - gs.things.list[1].y);
    game_tic(&gs, &IDLE);
    assert(gs.things.list[1].state != TH_STATE_STAND);
    bast_map_free(m);
}

/* Mutt wind-up: damage lands exactly windup tics after the wind-up starts
 * (20 at skill 2, 15 at skill 3), with the player in sight. */
static int windup_measure(int skill)
{
    bast_map *m = open_map(16, 8);
    add_player(m, 2, 3, 0);
    add_entity(m, BAST_ENT_MUTT_TROOPER, 8, 3, ANG180, BAST_ENT_SKILL_ALL);
    game_state gs;
    game_init(&gs, m, 1, skill, 0);
    gs.player.health = 1000; /* never dies here */
    int start = -1, hit = -1;
    for (int tic = 0; tic < 2000 && hit < 0; tic++) {
        const int32_t before = gs.player.health;
        const int was = gs.things.list[0].state;
        game_tic(&gs, &IDLE);
        if (was != TH_STATE_WINDUP && gs.things.list[0].state == TH_STATE_WINDUP)
            start = tic;
        if (gs.player.health < before) {
            assert(start >= 0);
            assert(was == TH_STATE_WINDUP && gs.things.list[0].state == TH_STATE_ATTACK);
            const int32_t d = before - gs.player.health;
            assert(d >= 3 && d <= 12);
            hit = tic;
        }
    }
    assert(hit >= 0);
    bast_map_free(m);
    return hit - start;
}

static void test_windup(void)
{
    const int w2 = windup_measure(2), w3 = windup_measure(3);
    printf("windup: Mutt %d tics on skill 2, %d on skill 3\n", w2, w3);
    assert(w2 == 20 && w3 == 15);
    game_state gs;
    memset(&gs, 0, sizeof gs);
    gs.skill = 3;
    assert(ai_windup_tics(&gs, BAST_ENT_CHIHUAHUA_CHOMPER) == 6);
    gs.skill = 2;
    assert(ai_windup_tics(&gs, BAST_ENT_CHIHUAHUA_CHOMPER) == 8);
}

/* A Chomper woken by a shot runs to a still player and bites 4..10. */
static void test_chomper(void)
{
    bast_map *m = open_map(20, 8);
    add_player(m, 2, 3, ANG90);
    add_entity(m, BAST_ENT_CHIHUAHUA_CHOMPER, 15, 4, ANG180, BAST_ENT_SKILL_ALL);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    game_tic(&gs, &FIRE); /* wakes it */
    int bite = -1;
    for (int tic = 1; tic < 200 && bite < 0; tic++) {
        const int32_t before = gs.player.health;
        game_tic(&gs, &IDLE);
        if (gs.player.health < before) {
            const int32_t d = before - gs.player.health;
            assert(d >= 4 && d <= 10);
            bite = tic;
        }
    }
    const thinker_t *t = &gs.things.list[0];
    const fix_t d = fix_dist(t->x - gs.player.x, t->y - gs.player.y);
    printf("chomper: bit at tic %d from %.3f u (centres)\n", bite, d / 65536.0);
    assert(bite > 0);
    /* It closed ~12.6 u at 10299/tic: at least 80 tics, and stopped at the
     * player's circle without overlapping it. */
    assert(bite >= 80 && bite < 120);
    assert(d >= t->radius + PLAYER_RADIUS && d - PLAYER_RADIUS <= 39321);
    bast_map_free(m);
}

/* A pillar between a Chomper and the player: it goes round, and the player
 * gets bitten. A Mutt never opens a keyed door; a Chomper opens an unkeyed
 * one. */
static void test_pillar(void)
{
    bast_map *m = open_map(16, 9);
    add_player(m, 2, 4, ANG90);
    add_entity(m, BAST_ENT_CHIHUAHUA_CHOMPER, 12, 4, ANG180, BAST_ENT_SKILL_ALL);
    for (int y = 2; y <= 6; y++) /* 1x5 pillar at x=7 */
        bast_map_cell_at(m, 7, y)->flags = BAST_CELL_SOLID;
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    game_tic(&gs, &FIRE);
    int bite = -1, max_dy = 0;
    for (int tic = 1; tic < 400 && bite < 0; tic++) {
        const int32_t before = gs.player.health;
        game_tic(&gs, &IDLE);
        const thinker_t *t = &gs.things.list[0];
        assert(!ai_circle_blocked(&gs, t->x, t->y, t->radius));
        const int dy = fix_abs(fix_to_int(t->y) - 4);
        if (dy > max_dy)
            max_dy = dy;
        if (gs.player.health < before)
            bite = tic;
    }
    printf("pillar: bitten at tic %d, detour %d tiles off the straight line\n", bite, max_dy);
    assert(bite > 0 && max_dy >= 3);
    bast_map_free(m);

    /* Door test: corridor y=3, door at x=8. */
    for (int keyed = 0; keyed <= 1; keyed++) {
        m = open_map(16, 7);
        for (int x = 1; x < 15; x++)
            for (int y = 1; y < 6; y++)
                if (y != 3)
                    bast_map_cell_at(m, x, y)->flags = BAST_CELL_SOLID;
        bast_map_cell *c = bast_map_cell_at(m, 8, 3);
        c->flags = BAST_CELL_DOOR;
        c->param = (uint8_t)(keyed ? 1 : 0);
        add_player(m, 2, 3, ANG90);
        add_entity(m, BAST_ENT_CHIHUAHUA_CHOMPER, 13, 3, ANG180, BAST_ENT_SKILL_ALL);
        game_init(&gs, m, 1, 2, 0);
        gs.things.list[0].state = TH_STATE_CHASE; /* awake (noise stops at doors) */
        int bitten = 0;
        for (int tic = 0; tic < 400; tic++) {
            const int32_t before = gs.player.health;
            game_tic(&gs, &IDLE);
            bitten |= gs.player.health < before;
        }
        printf("door: keyed=%d door state %d, bitten %d\n", keyed, gs.doors[0].state, bitten);
        assert(bitten == !keyed);
        const int tx = fix_to_int(gs.things.list[0].x);
        if (keyed)
            assert(gs.doors[0].state == DOOR_CLOSED && tx >= 9); /* never opened */
        else
            assert(tx < 8); /* opened it and went through */
        bast_map_free(m);
    }
}

/* Killing a Mutt drops a Charge Vial with 5 charges at its feet; it does
 * not count as an item. */
static void test_drop(void)
{
    bast_map *m = open_map(16, 8);
    add_player(m, 2, 3, 0);
    add_entity(m, BAST_ENT_MUTT_TROOPER, 5, 3, ANG180, BAST_ENT_SKILL_ALL);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    assert(gs.pickup_count == 0 && gs.items_total == 0);
    thinker_t *t = &gs.things.list[0];
    assert(!ai_damage(&gs, t, 5));
    assert(ai_damage(&gs, t, 50));
    assert(t->state == TH_STATE_DEAD && gs.things.kills == 1);
    assert(gs.pickup_count == 1);
    const pickup_t *pk = &gs.pickups[0];
    assert(pk->type == BAST_ENT_CHARGE_VIAL && pk->amount == 5 && pk->dropped);
    assert(pk->x == t->x && pk->y == t->y);
    /* Dead stays dead: no second drop. */
    assert(!ai_damage(&gs, t, 50) && gs.pickup_count == 1);
    /* Walk onto it: +5 charges, items unchanged. */
    const int32_t charges = gs.player.weapons.ammo[AM_CHARGES];
    gs.player.x = pk->x;
    gs.player.y = pk->y;
    game_tic(&gs, &IDLE);
    printf("drop: charges %d -> %d, items %d/%d\n", charges, gs.player.weapons.ammo[AM_CHARGES],
           gs.items_got, gs.items_total);
    assert(pk->taken && gs.player.weapons.ammo[AM_CHARGES] == charges + 5);
    assert(gs.items_got == 0 && gs.items_total == 0);
    /* Chompers drop nothing. */
    assert(enemy_info(BAST_ENT_CHIHUAHUA_CHOMPER)->drop == 0);
    bast_map_free(m);
}

/* A live Mutt blocks the player (playtester repro on #67): running east
 * into a Mutt 10 u ahead stops 0.5 u short of its centre. */
static void test_block(void)
{
    bast_map *m = open_map(18, 9);
    add_player(m, 2, 4, 0);
    add_entity(m, BAST_ENT_MUTT_TROOPER, 12, 4, ANG180, BAST_ENT_SKILL_ALL | BAST_ENT_AMBUSH);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    gs.player.health = 10000;
    const ticcmd run_east = { 127, 0, 0, BT_RUN };
    run(&gs, &run_east, 105);
    const thinker_t *t = &gs.things.list[0];
    const fix_t gap = t->x - gs.player.x;
    printf("block: player x %.3f, mutt x %.3f\n", gs.player.x / 65536.0, t->x / 65536.0);
    assert(fix_to_int(gs.player.x) <= fix_to_int(t->x));
    assert(gap >= t->radius + PLAYER_RADIUS - 1);
    bast_map_free(m);
}

/* A pushwall never slides into a live enemy (like the player and doors):
 * pushed east from (2,2) it would travel (3,2) -> (5,2), but a Mutt
 * stands at (5,2), so it stops at (4,2). The tile it left becomes open
 * to enemy routes (route_ok follows pushwall_blocks, not the spawn flag). */
static void test_pushwall(void)
{
    bast_map *m = open_map(12, 5);
    for (int x = 1; x <= 10; x++) {
        bast_map_cell_at(m, x, 1)->flags = BAST_CELL_SOLID;
        bast_map_cell_at(m, x, 3)->flags = BAST_CELL_SOLID;
    }
    bast_map_cell_at(m, 3, 2)->flags = BAST_CELL_PUSHWALL;
    add_player(m, 2, 2, 0);
    add_entity(m, BAST_ENT_MUTT_TROOPER, 5, 2, 0, BAST_ENT_SKILL_ALL | BAST_ENT_AMBUSH);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    const ticcmd use = { 0, 0, 0, BT_USE };
    game_tic(&gs, &use);
    assert(gs.pushwalls[0].state == PUSHWALL_MOVING);
    run(&gs, &IDLE, 200);
    const thinker_t *t = &gs.things.list[0];
    printf("pushwall: wall at (%d,%d) state %d, mutt tile (%d,%d) alive %d\n", gs.pushwalls[0].tx,
           gs.pushwalls[0].ty, gs.pushwalls[0].state, fix_to_int(t->x), fix_to_int(t->y),
           thinker_alive(t));
    assert(gs.pushwalls[0].tx == 4 && gs.pushwalls[0].ty == 2);
    assert(gs.pushwalls[0].state == PUSHWALL_DONE);
    assert(!game_tile_blocks(&gs, 3, 2) && game_tile_blocks(&gs, 4, 2));
    assert(!game_circle_overlaps_tile(t->x, t->y, t->radius, 4, 2));
    bast_map_free(m);
}

int main(int argc, char **argv)
{
    static const struct {
        const char *name;
        void (*fn)(void);
    } groups[] = {
        { "sight", test_sight },   { "ambush", test_ambush }, { "windup", test_windup },
        { "chomper", test_chomper }, { "pillar", test_pillar }, { "drop", test_drop },
        { "block", test_block }, { "pushwall", test_pushwall },
    };
    for (size_t i = 0; i < sizeof groups / sizeof groups[0]; i++) {
        if (argc < 2 || strcmp(argv[1], groups[i].name) == 0) {
            groups[i].fn();
            if (argc >= 2)
                return 0;
        }
    }
    if (argc >= 2) {
        fprintf(stderr, "test_ai: unknown group %s\n", argv[1]);
        return 1;
    }
    return 0;
}
