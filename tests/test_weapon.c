/* Weapons 1-4 and thinkers (weapons.md Stats / Behaviour notes, tech.md
 * Damage and State hash). Maps are built in memory with libbast. */
#include <assert.h>
#include <stdio.h>

#include "bast.h"
#include "game/state.h"

#define FX(i) fix_from_int(i)
#define HALF (FIX_ONE / 2)

static const ticcmd IDLE = { 0, 0, 0, 0 };
static const ticcmd FIRE = { 0, 0, 0, BT_FIRE };

static ticcmd select_cmd(int slot, int fire)
{
    ticcmd c = { 0, 0, 0, (uint8_t)((slot << BT_WEAPON_SHIFT) | (fire ? BT_FIRE : 0)) };
    return c;
}

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

static void add_entity(bast_map *m, uint16_t type, fix_t x, fix_t y, uint16_t flags)
{
    int i = bast_map_add_entity(m);
    assert(i >= 0);
    m->entities[i].type = type;
    m->entities[i].x = x;
    m->entities[i].y = y;
    m->entities[i].flags = flags;
}

/* Player at (2.5, 3.5) facing east, weapon `wp` in hand with plenty of ammo. */
static void setup(game_state *gs, const bast_map *m, int wp)
{
    game_init(gs, m, 1, 2, 0);
    gs->player.x = FX(2) + HALF;
    gs->player.y = FX(3) + HALF;
    gs->player.angle = 0;
    weapons_t *w = &gs->player.weapons;
    w->owned |= 1u << wp;
    w->ready = (uint8_t)wp;
    w->ammo[AM_CHARGES] = 200;
    w->ammo[AM_SHELLS] = 50;
}

static void run(game_state *gs, const ticcmd *cmd, int n)
{
    for (int i = 0; i < n; i++)
        game_tic(gs, cmd);
}

static void test_intervals(void)
{
    bast_map *m = open_map();
    static const int expect[] = { 0, 25, 30, 11, 88 }; /* ceil(350 / interval) */
    for (int wp = WP_CLAWS; wp <= WP_REPEATER; wp++) {
        game_state gs;
        setup(&gs, m, wp);
        run(&gs, &FIRE, 350);
        printf("weapon %d: interval %d, %u shots in 350 tics\n", wp, weapon_info[wp].interval,
               gs.player.weapons.shots);
        assert(gs.player.weapons.shots == (uint32_t)expect[wp]);
        assert(gs.player.weapons.ready == wp);
    }
    /* Exact spacing: pistol fires on tics 0, 12, 24. */
    game_state gs;
    setup(&gs, m, WP_PISTOL);
    for (int t = 0; t < 30; t++) {
        uint32_t before = gs.player.weapons.shots;
        game_tic(&gs, &FIRE);
        assert(gs.player.weapons.shots - before == (t % 12 == 0));
    }
    bast_map_free(m);
}

static void test_ammo(void)
{
    bast_map *m = open_map();
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    weapons_t *w = &gs.player.weapons;
    /* Start: Claws + pistol, pistol in hand, 50 charges. */
    assert(w->owned == ((1u << WP_CLAWS) | (1u << WP_PISTOL)));
    assert(w->ready == WP_PISTOL && w->ammo[AM_CHARGES] == 50);
    game_tic(&gs, &FIRE);
    assert(w->ammo[AM_CHARGES] == 49);
    assert(w->flash_tics == WEAPON_FLASH_TICS);
    run(&gs, &IDLE, 3);
    assert(w->flash_tics == 0);

    setup(&gs, m, WP_SCATTERGUN);
    w->ammo[AM_SHELLS] = 10;
    game_tic(&gs, &FIRE);
    assert(w->ammo[AM_SHELLS] == 9); /* one shell for 7 pellets */

    setup(&gs, m, WP_REPEATER);
    run(&gs, &FIRE, 8); /* shots on tics 0 and 4 */
    assert(w->ammo[AM_CHARGES] == 198);

    setup(&gs, m, WP_CLAWS);
    run(&gs, &FIRE, 30);
    assert(w->ammo[AM_CHARGES] == 200 && w->ammo[AM_SHELLS] == 50);
    bast_map_free(m);
}

static void test_switch(void)
{
    bast_map *m = open_map();
    game_state gs;
    setup(&gs, m, WP_PISTOL);
    gs.player.weapons.owned |= 1u << WP_SCATTERGUN;
    ticcmd sel = select_cmd(WP_SCATTERGUN, 1);
    game_tic(&gs, &sel); /* switch starts, fire held: blocked */
    int first_shot = -1;
    for (int t = 1; t < 20 && first_shot < 0; t++) {
        game_tic(&gs, &FIRE);
        if (gs.player.weapons.shots)
            first_shot = t;
    }
    printf("switch: first shot %d tics after the select\n", first_shot);
    assert(first_shot == WEAPON_SWITCH_TICS);
    assert(gs.player.weapons.ready == WP_SCATTERGUN);
    assert(gs.player.weapons.ammo[AM_SHELLS] == 49);

    /* Selecting a slot not owned does nothing. */
    setup(&gs, m, WP_PISTOL);
    sel = select_cmd(WP_REPEATER, 0);
    game_tic(&gs, &sel);
    assert(gs.player.weapons.switch_tics == 0 && gs.player.weapons.ready == WP_PISTOL);
    bast_map_free(m);
}

static void test_auto_switch(void)
{
    weapons_t w = { 0 };
    w.owned = 0xFE; /* slots 1-7 */
    w.ammo[AM_SCARABS] = 5;
    w.ammo[AM_CELLS] = 100;
    assert(weapon_auto_pick(&w) == WP_LANCE); /* 7 has ammo but is skipped */
    w.ammo[AM_CELLS] = 4;                     /* Lance needs 5 */
    assert(weapon_auto_pick(&w) == WP_CLAWS); /* 5 has ammo but is skipped */
    w.ammo[AM_SHELLS] = 1;
    assert(weapon_auto_pick(&w) == WP_SCATTERGUN);
    w.ammo[AM_CHARGES] = 1;
    assert(weapon_auto_pick(&w) == WP_REPEATER);

    /* In play: the last charge leaves the pistol dry, the scattergun comes up. */
    bast_map *m = open_map();
    game_state gs;
    setup(&gs, m, WP_PISTOL);
    gs.player.weapons.owned |= (1u << WP_SCATTERGUN) | (1u << WP_LAUNCHER);
    gs.player.weapons.ammo[AM_CHARGES] = 1;
    gs.player.weapons.ammo[AM_SCARABS] = 10;
    game_tic(&gs, &FIRE);
    assert(gs.player.weapons.ammo[AM_CHARGES] == 0);
    assert(gs.player.weapons.pending == WP_SCATTERGUN);
    run(&gs, &FIRE, WEAPON_SWITCH_TICS);
    assert(gs.player.weapons.ready == WP_SCATTERGUN);
    /* And with no shells either: Claws, never the Launcher. */
    setup(&gs, m, WP_PISTOL);
    gs.player.weapons.owned |= 1u << WP_LAUNCHER;
    gs.player.weapons.ammo[AM_CHARGES] = 1;
    gs.player.weapons.ammo[AM_SHELLS] = 0;
    gs.player.weapons.ammo[AM_SCARABS] = 10;
    run(&gs, &FIRE, 1 + WEAPON_SWITCH_TICS);
    assert(gs.player.weapons.ready == WP_CLAWS);
    bast_map_free(m);
}

static void test_scattergun_pellets(void)
{
    bast_map *m = open_map();
    add_entity(m, BAST_ENT_MUTT_TROOPER, FX(4) + HALF, FX(3) + HALF, BAST_ENT_SKILL_ALL);
    game_state gs;
    setup(&gs, m, WP_SCATTERGUN);
    assert(gs.things.count == 1);
    gs.things.list[0].health = gs.things.list[0].max_health = 1000;
    rng_t r = gs.rng;
    game_tic(&gs, &FIRE);
    /* 2 u away, max spread 5.6 deg is < 0.2 u sideways: all 7 pellets hit,
     * each drawing one spread and one damage number. */
    for (int i = 0; i < 2 * 7; i++)
        rng_next(&r);
    assert(gs.rng.state == r.state);
    int32_t dmg = 1000 - gs.things.list[0].health;
    printf("scattergun: one shot, 7 pellets, %d damage\n", dmg);
    assert(dmg >= 7 * 5 && dmg <= 7 * 9);
    assert(gs.player.weapons.shots == 1);
    bast_map_free(m);
}

static void test_wall_blocks(void)
{
    bast_map *m = open_map();
    add_entity(m, BAST_ENT_MUTT_TROOPER, FX(6) + HALF, FX(3) + HALF, BAST_ENT_SKILL_ALL);
    game_state gs;
    setup(&gs, m, WP_PISTOL);
    game_tic(&gs, &FIRE);
    assert(gs.things.list[0].health < 20); /* clear line: hit */

    bast_map_cell_at(m, 4, 3)->flags = BAST_CELL_SOLID;
    setup(&gs, m, WP_PISTOL);
    run(&gs, &FIRE, 60);
    assert(gs.player.weapons.shots == 5);
    assert(gs.things.list[0].health == 20); /* wall in front: never hit */
    fix_t wall;
    assert(weapon_trace(&gs, gs.player.x, gs.player.y, 0, HITSCAN_RANGE, 0, &wall) == 0);
    assert(wall == FX(1) + HALF);

    /* A closed door blocks too; an open one does not. */
    bast_map_cell_at(m, 4, 3)->flags = BAST_CELL_DOOR;
    setup(&gs, m, WP_PISTOL);
    assert(weapon_trace(&gs, gs.player.x, gs.player.y, 0, HITSCAN_RANGE, 0, NULL) == 0);
    bast_map_cell_at(m, 4, 3)->flags = BAST_CELL_DOOR | BAST_CELL_START_OPEN;
    assert(weapon_trace(&gs, gs.player.x, gs.player.y, 0, HITSCAN_RANGE, 0, NULL) == 1);
    bast_map_free(m);
}

static void test_claws_reach(void)
{
    bast_map *m = open_map();
    /* Mutt radius 0.25: centre 1.1 u away puts its edge at 0.85 > 0.75. */
    add_entity(m, BAST_ENT_MUTT_TROOPER, FX(2) + HALF + 72090, FX(3) + HALF, BAST_ENT_SKILL_ALL);
    game_state gs;
    setup(&gs, m, WP_CLAWS);
    run(&gs, &FIRE, 70);
    assert(gs.player.weapons.shots == 5);
    assert(gs.things.list[0].health == 20);

    /* 0.95 u: edge at 0.70, in reach. */
    gs.player.x = FX(2) + HALF + 72090 - 9830;
    run(&gs, &FIRE, 14);
    assert(gs.things.list[0].health < 20);
    bast_map_free(m);
}

static void test_kill(void)
{
    bast_map *m = open_map();
    add_entity(m, BAST_ENT_MUTT_TROOPER, FX(8) + HALF, FX(3) + HALF, BAST_ENT_SKILL_ALL);
    add_entity(m, BAST_ENT_MUTT_TROOPER, FX(9) + HALF, FX(3) + HALF, BAST_ENT_SKILL3);
    add_entity(m, BAST_ENT_WAYPOINT, FX(9) + HALF, FX(5) + HALF, BAST_ENT_SKILL_ALL);
    game_state gs;
    setup(&gs, m, WP_PISTOL);
    assert(gs.things.count == 1 && gs.things.kill_total == 1); /* skill 2 */
    thinker_t *t = thinker_get(&gs.things, 1);
    assert(t && t->max_health == 20 && t->radius == 16384 && t->state == TH_STATE_STAND);
    int tics = 0;
    while (t->state != TH_STATE_DEAD && tics < 200) {
        game_tic(&gs, &FIRE);
        tics++;
    }
    printf("kill: 20-HP Mutt dead after %d tics, health %d\n", tics, t->health);
    assert(t->state == TH_STATE_DEAD && t->health <= 0);
    assert(gs.things.kills == 1);
    int32_t hp = t->health;
    uint32_t shots = gs.player.weapons.shots;
    run(&gs, &FIRE, 48);
    assert(gs.player.weapons.shots == shots + 4);
    assert(t->health == hp && gs.things.kills == 1); /* corpses are not hit */

    /* Gib at HP <= -(max / 2). */
    thinkers_t th = { 0 };
    uint16_t id = thinker_spawn(&th, BAST_ENT_MUTT_TROOPER, 0, 0, 0);
    assert(id == 1 && thinker_spawn(&th, BAST_ENT_WAYPOINT, 0, 0, 0) == 0);
    assert(thinker_damage(&th, thinker_get(&th, id), 29) && !(th.list[0].flags & TH_FLAG_GIB));
    id = thinker_spawn(&th, BAST_ENT_MUTT_TROOPER, 0, 0, 0);
    assert(id == 2 && thinker_damage(&th, thinker_get(&th, id), 30));
    assert(th.list[1].flags & TH_FLAG_GIB);
    assert(th.kills == 2 && th.kill_total == 2);
    bast_map_free(m);
}

static void test_hash_covers_thinkers(void)
{
    bast_map *m = open_map();
    add_entity(m, BAST_ENT_MUTT_TROOPER, FX(8) + HALF, FX(3) + HALF, BAST_ENT_SKILL_ALL);
    game_state a, b;
    setup(&a, m, WP_PISTOL);
    setup(&b, m, WP_PISTOL);
    assert(game_hash(&a) == game_hash(&b));
    b.things.list[0].health--;
    assert(game_hash(&a) != game_hash(&b));
    setup(&b, m, WP_PISTOL);
    b.player.weapons.ammo[AM_CHARGES]--;
    assert(game_hash(&a) != game_hash(&b));
    bast_map_free(m);
}

static void test_weapon_pickup(void)
{
    bast_map *m = open_map();
    add_entity(m, BAST_ENT_HAIRBALL_SCATTERGUN, FX(3) + HALF, FX(3) + HALF, BAST_ENT_SKILL_ALL);
    add_entity(m, BAST_ENT_MUTT_TROOPER, FX(6) + HALF, FX(3) + HALF, BAST_ENT_SKILL_ALL);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    gs.player.x = FX(2) + HALF;
    gs.player.y = FX(3) + HALF;
    assert(gs.things.count == 2 && gs.things.kill_total == 1);
    /* The pickup is not shootable: the pistol hits the Mutt behind it. */
    game_tic(&gs, &FIRE);
    assert(gs.things.list[0].state == TH_STATE_STAND && gs.things.list[1].health < 20);
    gs.player.x = FX(3) + HALF - 30000; /* within 0.5 u */
    game_tic(&gs, &IDLE);
    assert(gs.things.list[0].state == TH_STATE_TAKEN);
    assert(gs.player.weapons.owned & (1u << WP_SCATTERGUN));
    assert(gs.player.weapons.ammo[AM_SHELLS] == 8 && gs.player.weapons.ready == WP_PISTOL);
    /* Owned: ammo only, capped at 50; full ammo leaves it on the floor. */
    weapons_t w = gs.player.weapons;
    w.ammo[AM_SHELLS] = 45;
    assert(weapon_give(&w, WP_SCATTERGUN) && w.ammo[AM_SHELLS] == 50);
    assert(!weapon_give(&w, WP_SCATTERGUN));
    bast_map_free(m);
}

int main(void)
{
    test_weapon_pickup();
    test_intervals();
    test_ammo();
    test_switch();
    test_auto_switch();
    test_scattergun_pellets();
    test_wall_blocks();
    test_claws_reach();
    test_kill();
    test_hash_covers_thinkers();
    puts("weapon: all tests passed");
    return 0;
}
