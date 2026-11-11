/* Enemy AI (enemies.md AI (shared), Stats; tech.md Skill). Maps are built
 * in memory with libbast. Usage: test_ai <group>. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "game/ai.h"
#include "game/enemy_info.h"
#include "game/items.h"
#include "game/projectile.h"
#include "game/sound.h"
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

/* Visibility cap (enemies.md, issue #221): a Mutt `gap` tiles east of the
 * player, every cell `dark`, woken by a shot. Runs up to `tics` tics and
 * returns the Mutt-to-player distance when it first starts a wind-up, or
 * -1 if it never does. *hurt gets the player's health loss. */
static fix_t first_windup_dist(int dark, int gap, int tics, int32_t *hurt)
{
    bast_map *m = open_map(gap + 6, 8);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < gap + 6; x++)
            bast_map_cell_at(m, x, y)->dark = (uint8_t)dark;
    add_player(m, 2, 3, ANG90);
    add_entity(m, BAST_ENT_MUTT_TROOPER, 2 + gap, 3, ANG180, BAST_ENT_SKILL_ALL);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    gs.player.health = 1000; /* stays alive whatever happens */
    const int32_t h0 = gs.player.health;
    game_tic(&gs, &FIRE); /* wakes it */
    fix_t at = -1;
    for (int tic = 0; tic < tics && at < 0; tic++) {
        game_tic(&gs, &IDLE);
        const thinker_t *t = &gs.things.list[0];
        if (t->state == TH_STATE_WINDUP)
            at = fix_dist(t->x - gs.player.x, t->y - gs.player.y);
    }
    *hurt = h0 - gs.player.health;
    bast_map_free(m);
    return at;
}

/* Rule: a hitscan wind-up starts only when 2 * dist + dark <= 26. */
static void test_hitscan_visibility(void)
{
    int32_t hurt;
    /* dark 10: black past 8 u, so not at 12 u; it walks in and fires
     * from 8 u or nearer. */
    fix_t d = first_windup_dist(10, 12, 600, &hurt);
    printf("hitscan_visibility: dark 10 from 12 u: wind-up at %.3f u\n", d / 65536.0);
    assert(d > 0 && 2 * (int64_t)d + 10 * FIX_ONE <= 26 * FIX_ONE);
    assert(d > FX(7)); /* and it does fire as soon as it is readable */
    /* dark 10 at 7 u: within the cap, fires at once. */
    d = first_windup_dist(10, 7, 40, &hurt);
    printf("hitscan_visibility: dark 10 from 7 u: wind-up at %.3f u\n", d / 65536.0);
    assert(d > FX(6) && d <= FX(7));
    /* dark 0 at 12 u: bright, fires from where it stands. */
    d = first_windup_dist(0, 12, 40, &hurt);
    printf("hitscan_visibility: dark 0 from 12 u: wind-up at %.3f u\n", d / 65536.0);
    assert(d > FX(11) && d <= FX(12));
    /* dark 28: never readable, never fires, even point blank. */
    d = first_windup_dist(28, 12, 600, &hurt);
    printf("hitscan_visibility: dark 28: wind-up %s, damage %d\n", d < 0 ? "never" : "yes",
           (int)hurt);
    assert(d < 0 && hurt == 0);
    /* Direct check of the formula at the boundary (dark 10: 8 u). */
    bast_map *m = open_map(20, 8);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 20; x++)
            bast_map_cell_at(m, x, y)->dark = 10;
    add_player(m, 2, 3, ANG90);
    add_entity(m, BAST_ENT_MUTT_TROOPER, 10, 3, ANG180, BAST_ENT_SKILL_ALL);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    thinker_t *t = &gs.things.list[0];
    t->x = gs.player.x + FX(8);
    assert(ai_hitscan_visible(&gs, t));
    t->x += 1;
    assert(!ai_hitscan_visible(&gs, t));
    gs.lights_alt = 1; /* dark_alt (0) after LIGHTS */
    assert(ai_hitscan_visible(&gs, t));
    bast_map_free(m);
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

/* ---- Pomeranian Plasmagunner and Dachshund Howitzer (issue #95) ---- */

#define POM_ID 1 /* the enemy is the first thinker in these maps */

/* Forces enemy `t` into its wind-up now, as an aggression roll would. */
static void force_windup(game_state *gs, thinker_t *t)
{
    t->state = TH_STATE_WINDUP;
    t->tics = ai_windup_tics(gs, t->type);
    t->angle = angle_of(gs->player.x - t->x, gs->player.y - t->y);
}

static int count_type(const game_state *gs, uint16_t type)
{
    int n = 0;
    for (int i = 0; i < gs->things.count; i++)
        if (gs->things.list[i].type == type && gs->things.list[i].state != TH_STATE_FREE)
            n++;
    return n;
}

/* A bolt flies at exactly 14980 per tic (8 u/s), 8..16 damage, and is
 * fired by the Pomeranian (its owner). */
static void test_pom_speed(void)
{
    bast_map *m = open_map(24, 5);
    add_entity(m, BAST_ENT_POMERANIAN_PLASMAGUNNER, 18, 2, ANG180, BAST_ENT_SKILL_ALL);
    add_player(m, 2, 2, 0);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    force_windup(&gs, &gs.things.list[0]);
    int tic = 0;
    while (count_type(&gs, TH_TYPE_PLASMA_BOLT) == 0 && tic++ < 100)
        game_tic(&gs, &IDLE);
    assert(tic == 16);
    const projectile_info_t *pi = projectile_info(TH_TYPE_PLASMA_BOLT);
    assert(pi && pi->speed == 14980 && pi->dmg_base == 8 && pi->dmg_spread == 8);
    assert(pi->splash == 0 && pi->fx == TH_TYPE_PLASMA_HIT);
    const thinker_t *b = &gs.things.list[1];
    assert(b->type == TH_TYPE_PLASMA_BOLT && b->owner == POM_ID);
    assert(b->momx == -14980 && b->momy == 0);
    const fix_t x0 = b->x;
    game_tic(&gs, &IDLE);
    printf("pom_speed: bolt x %d -> %d (dx %d)\n", x0, b->x, b->x - x0);
    assert(b->x - x0 == -14980 && b->y == CENTRE(2));
    bast_map_free(m);
}

/* Two bolts, 16 tics after the wind-up starts and 10 tics apart, each
 * aimed at the player at its fire tic. */
static void test_pom_pair(void)
{
    bast_map *m = open_map(24, 5);
    add_entity(m, BAST_ENT_POMERANIAN_PLASMAGUNNER, 18, 2, ANG180, BAST_ENT_SKILL_ALL);
    add_player(m, 2, 2, 0);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    force_windup(&gs, &gs.things.list[0]);
    int fired[2] = { -1, -1 }, nf = 0, seen = 1;
    for (int tic = 1; tic <= 40; tic++) {
        game_tic(&gs, &IDLE);
        if (gs.things.count > seen) {
            assert(gs.things.list[seen].type == TH_TYPE_PLASMA_BOLT && nf < 2);
            fired[nf++] = tic;
            seen = gs.things.count;
        }
    }
    printf("pom_pair: bolts at tics %d and %d\n", fired[0], fired[1]);
    assert(nf == 2 && fired[0] == 16 && fired[1] == 26);
    bast_map_free(m);
}

/* Player 8 u from a Pomeranian that winds up at tic 0. Returns the damage
 * the player took by the time both bolts are spent. side: strafe input. */
static int32_t pom_volley(int side)
{
    bast_map *m = open_map(20, 64);
    add_entity(m, BAST_ENT_POMERANIAN_PLASMAGUNNER, 10, 58, ANG180, BAST_ENT_SKILL_ALL);
    add_player(m, 2, 58, 0); /* faces east: strafing right walks south */
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    gs.player.health = 1000;
    force_windup(&gs, &gs.things.list[0]);
    const ticcmd strafe = { 0, (int8_t)side, 0, 0 };
    int spent = 0;
    for (int tic = 0; tic < 200 && spent < 2; tic++) {
        game_tic(&gs, &strafe);
        spent = 0;
        for (int i = 1; i < gs.things.count && i < 3; i++)
            spent += gs.things.list[i].type != TH_TYPE_PLASMA_BOLT;
        if (gs.things.count < 3)
            spent = 0;
    }
    assert(spent == 2);
    printf("pom_volley side %d: player y %.2f, damage %d\n", side, gs.player.y / 65536.0,
           1000 - gs.player.health);
    bast_map_free(m);
    return 1000 - gs.player.health;
}

static void test_pom_dodge(void)
{
    assert(pom_volley(127) == 0); /* walk-speed strafe: both bolts miss */
}

static void test_pom_still(void)
{
    const int32_t d = pom_volley(0); /* both hit: 8..16 each */
    assert(d >= 16 && d <= 32);
}

/* The Dachshund marks the player's position: the shell lands exactly 42
 * tics after the reticle appears, at the reticle, splash 40 r1.5. A player
 * who walked away is unhurt; one who stayed takes the full 40. */
static int32_t dach_shell(int side, int *spawn_tic, int *land_tic)
{
    bast_map *m = open_map(20, 64);
    add_entity(m, BAST_ENT_DACHSHUND_HOWITZER, 10, 58, ANG180, BAST_ENT_SKILL_ALL);
    add_player(m, 2, 58, 0);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    gs.player.health = 1000;
    force_windup(&gs, &gs.things.list[0]);
    const ticcmd strafe = { 0, (int8_t)side, 0, 0 };
    fix_t rx = 0, ry = 0, px = 0, py = 0;
    *spawn_tic = *land_tic = -1;
    int32_t hurt_at_land = 0;
    for (int tic = 1; tic <= 200 && *land_tic < 0; tic++) {
        const int32_t before = gs.player.health;
        game_tic(&gs, &strafe);
        const thinker_t *r = gs.things.count > 1 ? &gs.things.list[1] : NULL;
        if (r && *spawn_tic < 0) {
            assert(r->type == TH_TYPE_RETICLE && r->owner == 1);
            *spawn_tic = tic;
            rx = r->x;
            ry = r->y;
            px = gs.player.x;
            py = gs.player.y;
        }
        if (r && r->type != TH_TYPE_RETICLE) {
            *land_tic = tic;
            assert(r->type == TH_TYPE_EXPLOSION_SMALL && r->x == rx && r->y == ry);
            hurt_at_land = before - gs.player.health;
        }
    }
    assert(rx == px && ry == py); /* where the player stood at the launch */
    printf("dach_shell side %d: reticle (%.2f, %.2f) at tic %d, lands tic %d, player (%.2f, "
           "%.2f), damage %d\n",
           side, rx / 65536.0, ry / 65536.0, *spawn_tic, *land_tic, gs.player.x / 65536.0,
           gs.player.y / 65536.0, hurt_at_land);
    assert(1000 - gs.player.health == hurt_at_land); /* nothing else hurt */
    bast_map_free(m);
    return hurt_at_land;
}

static void test_dach_mortar(void)
{
    int s, l;
    assert(dach_shell(0, &s, &l) == 40);
    assert(s == 14 && l == s + 42);
    assert(dach_shell(127, &s, &l) == 0);
    assert(s == 14 && l == s + 42);
}

/* A Dachshund 3 u from the player backs away; one 8 u away holds its
 * ground; one 14 u away closes in. Measured over 16 tics with AMBUSH
 * awake enemies that cannot roll an attack before moving. */
static fix_t dach_move(int gap)
{
    bast_map *m = open_map(40, 5);
    add_player(m, 2, 2, 0);
    add_entity(m, BAST_ENT_DACHSHUND_HOWITZER, 2 + gap, 2, ANG180, BAST_ENT_SKILL_ALL);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    thinker_t *t = &gs.things.list[0];
    t->state = TH_STATE_CHASE;
    t->movecount = 100; /* no attack decision during the measure */
    const fix_t d0 = t->x - gs.player.x;
    run(&gs, &IDLE, 16);
    printf("dach_move gap %d: %.3f -> %.3f u, state %d\n", gap, d0 / 65536.0,
           (t->x - gs.player.x) / 65536.0, t->state);
    assert(t->state == TH_STATE_CHASE && t->y == CENTRE(2));
    bast_map_free(m);
    return t->x - gs.player.x - d0;
}

static void test_dach_retreat(void)
{
    assert(dach_move(3) >= 16 * 2247 - 1); /* full speed away */
    assert(dach_move(8) == 0);
    assert(dach_move(14) < 0);
}

/* Sabretooth wind-ups are 25% shorter, rounded down: 16 -> 12, 14 -> 10. */
static void test_pom_windup(void)
{
    bast_map *m = open_map(8, 8);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    assert(ai_windup_tics(&gs, BAST_ENT_POMERANIAN_PLASMAGUNNER) == 16);
    assert(ai_windup_tics(&gs, BAST_ENT_DACHSHUND_HOWITZER) == 14);
    game_init(&gs, m, 1, 3, 0);
    assert(ai_windup_tics(&gs, BAST_ENT_POMERANIAN_PLASMAGUNNER) == 12);
    assert(ai_windup_tics(&gs, BAST_ENT_DACHSHUND_HOWITZER) == 10);
    assert(ai_windup_tics(&gs, BAST_ENT_MUTT_TROOPER) == 15);
    assert(ai_windup_tics(&gs, BAST_ENT_CHIHUAHUA_CHOMPER) == 6);
    bast_map_free(m);
}

static void test_dach_windup(void)
{
    /* Measured: the reticle appears 10 tics into a Sabretooth wind-up. */
    bast_map *m = open_map(20, 5);
    add_entity(m, BAST_ENT_DACHSHUND_HOWITZER, 10, 2, ANG180, BAST_ENT_SKILL_ALL);
    add_player(m, 2, 2, 0);
    game_state gs;
    game_init(&gs, m, 1, 3, 0);
    force_windup(&gs, &gs.things.list[0]);
    int tic = 0;
    while (count_type(&gs, TH_TYPE_RETICLE) == 0 && tic++ < 100)
        game_tic(&gs, &IDLE);
    printf("dach_windup: sabretooth reticle at tic %d\n", tic);
    assert(tic == 10);
    bast_map_free(m);
}

/* Headbutt 5..10 when the player is within 1.5 u. */
static void test_dach_headbutt(void)
{
    bast_map *m = open_map(20, 5);
    add_entity(m, BAST_ENT_DACHSHUND_HOWITZER, 4, 2, ANG180, BAST_ENT_SKILL_ALL);
    add_player(m, 2, 2, 0);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    gs.player.x = CENTRE(3) - HALF / 2; /* 1.25 u between centres */
    thinker_t *t = &gs.things.list[0];
    t->state = TH_STATE_CHASE;
    game_tic(&gs, &IDLE);
    assert(t->state == TH_STATE_WINDUP);
    int launches = 0; /* audio: a headbutt is not a mortar shot */
    for (int i = 0; i < 14; i++) {
        game_tic(&gs, &IDLE);
        for (int k = 0; k < gs.sounds.count; k++)
            launches += gs.sounds.ev[k].id == SFX_mortar_launch;
    }
    printf("dach_headbutt: damage %d, reticles %d, mortar_launch %d\n",
           100 - gs.player.health, count_type(&gs, TH_TYPE_RETICLE), launches);
    assert(launches == 0);
    assert(gs.player.health <= 95 && gs.player.health >= 90);
    assert(count_type(&gs, TH_TYPE_RETICLE) == 0);
    bast_map_free(m);
}

/* Records the tics on which sound `sfx` was queued; with `at` set, only
 * events at that exact position. */
static int sound_tics(const game_state *gs, int sfx, const fix_t *at, int tic, int *out, int n,
                      int max)
{
    for (int i = 0; i < gs->sounds.count; i++) {
        const sound_event *e = &gs->sounds.ev[i];
        if (e->id == sfx && n < max && (!at || (e->x == at[0] && e->y == at[1])))
            out[n++] = tic;
    }
    return n;
}

/* audio.md: plasma_fire on each bolt's fire tic, plasma_hit where each
 * bolt strikes the standing player. Every volley over 120 tics counts. */
static void test_pom_sound(void)
{
    bast_map *m = open_map(24, 5);
    add_entity(m, BAST_ENT_POMERANIAN_PLASMAGUNNER, 18, 2, ANG180, BAST_ENT_SKILL_ALL);
    add_player(m, 2, 2, 0);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    gs.player.health = 1000;
    force_windup(&gs, &gs.things.list[0]);
    int fire[16], hit[16], bolts[16], nf = 0, nh = 0, nb = 0, nbolt_hit = 0;
    uint8_t was_bolt[MAX_THINKERS] = { 0 };
    for (int tic = 1; tic <= 120; tic++) {
        game_tic(&gs, &IDLE);
        for (int i = 0; i < gs.things.count; i++) {
            const thinker_t *t = &gs.things.list[i];
            const int bolt = t->type == TH_TYPE_PLASMA_BOLT && t->state == TH_STATE_STAND;
            if (bolt && !was_bolt[i] && nb < 16)
                bolts[nb++] = tic; /* a new bolt in this slot */
            if (!bolt && was_bolt[i] && t->type == TH_TYPE_PLASMA_HIT)
                nbolt_hit++; /* a bolt turned into its impact effect */
            was_bolt[i] = (uint8_t)bolt;
        }
        nf = sound_tics(&gs, SFX_plasma_fire, NULL, tic, fire, nf, 16);
        nh = sound_tics(&gs, SFX_plasma_hit, NULL, tic, hit, nh, 16);
    }
    printf("pom_sound: bolts at");
    for (int i = 0; i < nb; i++)
        printf(" %d", bolts[i]);
    printf(", plasma_fire at");
    for (int i = 0; i < nf; i++)
        printf(" %d", fire[i]);
    printf(", %d impacts, plasma_hit at", nbolt_hit);
    for (int i = 0; i < nh; i++)
        printf(" %d", hit[i]);
    printf("\n");
    assert(nb >= 4 && nf == nb);
    for (int i = 0; i < nb; i++)
        assert(fire[i] == bolts[i]);
    assert(fire[0] == 16 && fire[1] == 26);
    assert(nh >= 2 && nh == nbolt_hit && hit[0] > fire[0] && hit[1] > fire[1]);
    bast_map_free(m);
}

/* audio.md: mortar_launch with the reticle, reticle_beep every 8 tics
 * while the reticle lives (from its spawn), mortar_land when the shell
 * lands. The player strafes so later reticles sit elsewhere; beeps are
 * counted at the first reticle's position only. */
static void test_dach_sound(void)
{
    bast_map *m = open_map(20, 64);
    add_entity(m, BAST_ENT_DACHSHUND_HOWITZER, 10, 58, ANG180, BAST_ENT_SKILL_ALL);
    add_player(m, 2, 58, 0);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    gs.player.health = 1000;
    force_windup(&gs, &gs.things.list[0]);
    const ticcmd strafe = { 0, 127, 0, 0 };
    int launch[8], beep[16], land[8], nl = 0, nb = 0, nd = 0;
    fix_t at[2] = { 0, 0 };
    int have_at = 0;
    for (int tic = 1; tic <= 70; tic++) {
        game_tic(&gs, &strafe);
        if (!have_at && gs.things.count > 1 && gs.things.list[1].type == TH_TYPE_RETICLE) {
            at[0] = gs.things.list[1].x;
            at[1] = gs.things.list[1].y;
            have_at = 1;
        }
        nl = sound_tics(&gs, SFX_mortar_launch, NULL, tic, launch, nl, 8);
        if (have_at) {
            nb = sound_tics(&gs, SFX_reticle_beep, at, tic, beep, nb, 16);
            nd = sound_tics(&gs, SFX_mortar_land, at, tic, land, nd, 8);
        }
    }
    printf("dach_sound: mortar_launch at %d, first reticle's beeps at", launch[0]);
    for (int i = 0; i < nb; i++)
        printf(" %d", beep[i]);
    printf(", mortar_land at %d\n", land[0]);
    assert(have_at && nl >= 1 && launch[0] == 14);
    assert(nd == 1 && land[0] == 14 + MORTAR_DELAY_TICS);
    int expect = 0;
    for (int t = 14; t < land[0]; t += 8)
        assert(expect < nb && beep[expect++] == t);
    assert(nb == expect && nb == 6); /* 14, 22, 30, 38, 46, 54 */
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
        { "pom_speed", test_pom_speed }, { "pom_pair", test_pom_pair },
        { "pom_dodge", test_pom_dodge }, { "pom_still", test_pom_still },
        { "pom_windup", test_pom_windup }, { "dach_mortar", test_dach_mortar },
        { "dach_retreat", test_dach_retreat }, { "dach_windup", test_dach_windup },
        { "dach_headbutt", test_dach_headbutt }, { "pom_sound", test_pom_sound },
        { "dach_sound", test_dach_sound },
        { "hitscan_visibility", test_hitscan_visibility },
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
