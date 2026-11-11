/* Decorations (issue #131; items.md Decorations): entity names, solid
 * props, the hanging banner, the breakable papyrus pot, enemies blocked by
 * props, and the live render list. One ctest per group:
 * test_decorations <group>. Maps are built in memory with libbast. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "game/ai.h"
#include "game/items.h"
#include "game/projectile.h"
#include "game/state.h"

#define FX(i) fix_from_int(i)
#define HALF (FIX_ONE / 2)

static const ticcmd IDLE = { 0, 0, 0, 0 };
static const ticcmd FIRE = { 0, 0, 0, BT_FIRE };
static const ticcmd WALK = { 127, 0, 0, 0 };

static const char *const NAMES[] = {
    "KIBBLE_BARREL", "PAPYRUS_POT", "OBELISK_STUB", "PILLAR_SAND", "BRAZIER",
    "CAT_STATUE",    "CRATE",       "FLOODLIGHT",   "DOG_BANNER",  "SERVER_RACK",
};
#define NDECOR ((int)(sizeof NAMES / sizeof NAMES[0]))

/* 32x8 map, solid border, everything else open. */
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

static void add_entity(bast_map *m, uint16_t type, fix_t x, fix_t y)
{
    int i = bast_map_add_entity(m);
    assert(i >= 0);
    m->entities[i].type = type;
    m->entities[i].x = x;
    m->entities[i].y = y;
    m->entities[i].flags = BAST_ENT_SKILL_ALL;
}

/* Player at (2.5, 3.5) facing east (angle 0). */
static void setup(game_state *gs, const bast_map *m)
{
    game_init(gs, m, 1, 2, 0);
    gs->player.x = FX(2) + HALF;
    gs->player.y = FX(3) + HALF;
    gs->player.angle = 0;
}

/* Names 96..105 match bast.h and round-trip; the limit is one past them. */
static void t_names(void)
{
    for (int i = 0; i < NDECOR; i++) {
        const int type = BAST_ENT_KIBBLE_BARREL + i;
        assert(bast_entity_type_name(type) && !strcmp(bast_entity_type_name(type), NAMES[i]));
        assert(bast_entity_type_from_name(NAMES[i]) == type);
        assert(thinker_is_decor(type));
    }
    assert(BAST_ENT_SERVER_RACK == 105 && BAST_ENT_TYPE_LIMIT == 106);
    assert(!thinker_is_decor(BAST_ENT_NINTH_LIFE) && !thinker_is_decor(BAST_ENT_TYPE_LIMIT));
    printf("decor_names: %d decoration types 96..105 named\n", NDECOR);
}

/* Every solid prop stops the player walking into it at 0.25 u plus the
 * player radius; the banner is walked through. */
static void t_solid(void)
{
    for (int i = 0; i < NDECOR; i++) {
        const uint16_t type = (uint16_t)(BAST_ENT_KIBBLE_BARREL + i);
        bast_map *m = open_map();
        add_entity(m, type, FX(5) + HALF, FX(3) + HALF);
        game_state gs;
        setup(&gs, m);
        assert(gs.things.count == 1 && gs.things.list[0].type == type);
        for (int k = 0; k < 105; k++)
            game_tic(&gs, &WALK);
        const fix_t stop = FX(5) + HALF - PROP_RADIUS - PLAYER_RADIUS;
        printf("decor_solid: %-13s player x=%d (stop at %d)\n", NAMES[i], gs.player.x, stop);
        if (type == BAST_ENT_DOG_BANNER) {
            assert(gs.things.list[0].state == TH_STATE_PROP);
            assert(gs.player.x > FX(6) + HALF); /* walked through and past it */
        } else {
            assert(gs.things.list[0].radius == PROP_RADIUS);
            assert(gs.player.x <= stop && gs.player.x >= stop - 2);
        }
        bast_map_free(m);
    }
}

/* A pistol shot breaks the pot: DEAD with gibs, not a kill, no drop, its
 * gib animation runs to the last frame, and it stops blocking. */
static void t_pot(void)
{
    bast_map *m = open_map();
    add_entity(m, BAST_ENT_PAPYRUS_POT, FX(6) + HALF, FX(3) + HALF);
    game_state gs;
    setup(&gs, m);
    thinker_t *pot = thinker_get(&gs.things, 1);
    assert(pot && pot->type == BAST_ENT_PAPYRUS_POT && pot->health == POT_HP);
    assert(gs.player.weapons.ready == WP_PISTOL);
    const int pickups = gs.pickup_count;
    render_thing rt[8];
    assert(items_render_things(&gs, rt, 8) == 1 && rt[0].gib == 0);

    int k = 0;
    for (; k < 40 && pot->state != TH_STATE_DEAD; k++)
        game_tic(&gs, &FIRE);
    printf("decor_pot: broke after %d tic(s), health %d, kills %d/%d, pickups %d\n", k,
           pot->health, gs.things.kills, gs.things.kill_total, gs.pickup_count);
    assert(pot->state == TH_STATE_DEAD && (pot->flags & TH_FLAG_GIB));
    assert(gs.things.kills == 0 && gs.things.kill_total == 0);
    assert(gs.pickup_count == pickups); /* drops nothing */
    assert(thinker_gib_frame(pot) == 0);
    assert(items_render_things(&gs, rt, 8) == 1 && rt[0].gib == 1);
    for (int t = 0; t < PROP_GIB_TICS; t++)
        game_tic(&gs, &IDLE);
    assert(pot->tics == 0 && thinker_gib_frame(pot) == PROP_GIB_FRAMES - 1);
    assert(items_render_things(&gs, rt, 8) == 1 && rt[0].gib == PROP_GIB_FRAMES);

    for (int t = 0; t < 70; t++)
        game_tic(&gs, &WALK);
    printf("decor_pot: then walked to x=%d past the pot at %d\n", gs.player.x, pot->x);
    assert(gs.player.x > pot->x + HALF);
    bast_map_free(m);
}

/* Splash breaks a pot too; the solid inert props take no damage. */
static void t_splash(void)
{
    bast_map *m = open_map();
    add_entity(m, BAST_ENT_PAPYRUS_POT, FX(10) + HALF, FX(3) + HALF);
    add_entity(m, BAST_ENT_CRATE, FX(10) + HALF, FX(4) + HALF);
    game_state gs;
    setup(&gs, m);
    splash_damage(&gs, FX(10) + HALF, FX(4), 64, SPLASH_RADIUS_1_5, 0);
    const thinker_t *pot = thinker_get(&gs.things, 1), *crate = thinker_get(&gs.things, 2);
    printf("decor_splash: pot state %d, crate state %d health %d\n", pot->state, crate->state,
           crate->health);
    assert(pot->state == TH_STATE_DEAD);
    assert(crate->state == TH_STATE_STAND && thinker_alive(crate));
    bast_map_free(m);
}

/* A chasing Mutt Trooper walking at the player never enters a crate in
 * its way. */
static void t_enemy(void)
{
    bast_map *m = open_map();
    add_entity(m, BAST_ENT_MUTT_TROOPER, FX(12) + HALF, FX(3) + HALF);
    add_entity(m, BAST_ENT_CRATE, FX(9) + HALF, FX(3) + HALF);
    game_state gs;
    setup(&gs, m);
    thinker_t *mutt = thinker_get(&gs.things, 1);
    const thinker_t *crate = thinker_get(&gs.things, 2);
    mutt->state = TH_STATE_CHASE;
    mutt->target = 0;
    const int64_t rr = (int64_t)mutt->radius + crate->radius;
    int64_t closest = INT64_MAX;
    const fix_t x0 = mutt->x;
    for (int k = 0; k < 140; k++) {
        game_tic(&gs, &IDLE);
        const int64_t dx = (int64_t)mutt->x - crate->x, dy = (int64_t)mutt->y - crate->y;
        const int64_t d2 = dx * dx + dy * dy;
        if (d2 < closest)
            closest = d2;
        assert(d2 >= rr * rr);
    }
    printf("decor_enemy: mutt moved from x=%d to (%d, %d), closest^2 %lld >= %lld\n", x0,
           mutt->x, mutt->y, (long long)closest, (long long)(rr * rr));
    assert(mutt->x != x0 || mutt->y != FX(3) + HALF); /* it did move */
    bast_map_free(m);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *g = argc > 1 ? argv[1] : "all";
    const int all = strcmp(g, "all") == 0;
    if (all || !strcmp(g, "names")) t_names();
    if (all || !strcmp(g, "solid")) t_solid();
    if (all || !strcmp(g, "pot")) t_pot();
    if (all || !strcmp(g, "splash")) t_splash();
    if (all || !strcmp(g, "enemy")) t_enemy();
    return 0;
}
