/* Projectiles, splash damage, Scarab Launcher, kibble barrels (issue #93;
 * weapons.md Scarab Launcher, items.md Decorations). One ctest per group:
 * test_projectile <group>. Maps are built in memory with libbast. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "game/ai.h"
#include "game/door.h"
#include "game/items.h"
#include "game/projectile.h"
#include "game/state.h"

#define FX(i) fix_from_int(i)
#define HALF (FIX_ONE / 2)

static const ticcmd IDLE = { 0, 0, 0, 0 };
static const ticcmd FIRE = { 0, 0, 0, BT_FIRE };

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

/* Player at (x, 3.5) facing `angle`, launcher in hand with 10 scarabs. */
static void setup(game_state *gs, const bast_map *m, fix_t x, angle_t angle)
{
    game_init(gs, m, 1, 2, 0);
    gs->player.x = x;
    gs->player.y = FX(3) + HALF;
    gs->player.angle = angle;
    weapons_t *w = &gs->player.weapons;
    w->owned |= 1u << WP_LAUNCHER;
    w->ready = WP_LAUNCHER;
    w->ammo[AM_SCARABS] = 10;
}

/* The first thinker of `type` in any state but FREE, or NULL. */
static thinker_t *find(game_state *gs, uint16_t type)
{
    for (int i = 0; i < gs->things.count; i++)
        if (gs->things.list[i].type == type && gs->things.list[i].state != TH_STATE_FREE)
            return &gs->things.list[i];
    return NULL;
}

static void t_travel(void)
{
    bast_map *m = open_map();
    game_state gs;
    setup(&gs, m, FX(2) + HALF, 0);
    game_tic(&gs, &FIRE);
    assert(gs.player.weapons.ammo[AM_SCARABS] == 9);
    assert(gs.player.weapons.refire_tics == 20);
    thinker_t *s = find(&gs, TH_TYPE_SCARAB_PROJ);
    assert(s && s->owner == TH_ID_PLAYER && s->z == PLAYER_EYE_Z);
    const fix_t x0 = s->x;
    assert(x0 == gs.player.x && s->momx == 26214 && s->momy == 0);
    for (int k = 1; k <= 20; k++) {
        game_tic(&gs, &IDLE);
        assert(s->type == TH_TYPE_SCARAB_PROJ);
        assert(s->x == x0 + k * 26214);
        assert(s->y == FX(3) + HALF);
    }
    /* Diagonal: per-tic step is exactly the fixed velocity. */
    setup(&gs, m, FX(2) + HALF, 8192); /* 45 degrees */
    gs.player.y = FX(1) + HALF;
    game_tic(&gs, &FIRE);
    s = find(&gs, TH_TYPE_SCARAB_PROJ);
    const fix_t mx = s->momx, my = s->momy, sx = s->x, sy = s->y;
    assert(mx == fix_mul(26214, fine_cos(8192)) && mx > 0 && my > 0);
    game_tic(&gs, &IDLE);
    game_tic(&gs, &IDLE);
    assert(s->x == sx + 2 * mx && s->y == sy + 2 * my);
    printf("proj_travel: 20 tics x %d = %d, start %d end %d\n", 26214, 20 * 26214, x0, x0 + 20 * 26214);
    bast_map_free(m);
}

static void t_wall(void)
{
    bast_map *m = open_map();
    game_state gs;
    setup(&gs, m, FX(20) + HALF, 0); /* wall at x = 31: 10.5 u east */
    gs.player.health = 100;
    game_tic(&gs, &FIRE);
    thinker_t *s = find(&gs, TH_TYPE_SCARAB_PROJ);
    assert(s);
    const uint16_t id = (uint16_t)(s - gs.things.list + 1);
    int tics = 0;
    while (s->type == TH_TYPE_SCARAB_PROJ) {
        game_tic(&gs, &IDLE);
        assert(++tics < 100);
    }
    /* It became a small explosion against the wall, never inside it. */
    assert(s->type == TH_TYPE_EXPLOSION_SMALL && s->state == TH_STATE_STAND);
    assert(s->x + s->radius <= FX(31) && s->x + s->radius + 26214 / PROJ_SUBSTEPS > FX(31));
    assert(s->momx == 0 && s->momy == 0);
    const fix_t x = s->x;
    printf("proj_wall: exploded after %d tics at x=%d (wall face %d, radius %d)\n", tics, x, FX(31),
           s->radius);
    assert(gs.player.health == 100); /* 10 u away */
    /* The effect stays put for 8 frames x 3 tics, then the slot is free
     * and the next scarab reuses it. */
    for (int k = 0; k < 8 * FX_TICS_PER_FRAME - 1; k++) {
        game_tic(&gs, &IDLE);
        assert(s->state == TH_STATE_STAND && s->x == x);
    }
    game_tic(&gs, &IDLE);
    assert(s->state == TH_STATE_FREE);
    const int count = gs.things.count;
    game_tic(&gs, &FIRE);
    assert(gs.things.count == count);
    assert(thinker_get(&gs.things, id)->type == TH_TYPE_SCARAB_PROJ);
    bast_map_free(m);
}

/* Woofu (2400 HP) at (tx, 3.5); returns the damage a 96 / 1.5 u splash at
 * (ex, 3.5) deals to it. */
static int32_t splash_on_woofu(bast_map *m, fix_t ex, fix_t tx)
{
    game_state gs;
    setup(&gs, m, FX(28) + HALF, 0);
    uint16_t id = thinker_spawn(&gs.things, BAST_ENT_WOOFU, tx, FX(3) + HALF, 0);
    thinker_t *t = thinker_get(&gs.things, id);
    splash_damage(&gs, ex, FX(3) + HALF, 96, SPLASH_RADIUS_1_5, 0);
    assert(gs.player.health == 100);
    return 2400 - t->health;
}

static void t_splash(void)
{
    bast_map *m = open_map();
    const int32_t d0 = splash_on_woofu(m, FX(10), FX(10));
    const int32_t d75 = splash_on_woofu(m, FX(10), FX(10) + 49152);
    const int32_t d15 = splash_on_woofu(m, FX(10), FX(10) + 98304);
    printf("proj_splash: d=0 -> %d, d=0.75 -> %d, d=1.5 -> %d\n", d0, d75, d15);
    assert(d0 == 96);
    assert(d75 >= 47 && d75 <= 49);
    assert(d15 == 0);
    /* Gib rule: a Mutt (20 HP) in the centre of a 96 splash gibs. */
    game_state gs;
    setup(&gs, m, FX(28) + HALF, 0);
    thinker_t *mutt = thinker_get(&gs.things, thinker_spawn(&gs.things, BAST_ENT_MUTT_TROOPER,
                                                             FX(5), FX(3), 0));
    splash_damage(&gs, FX(5), FX(3), 96, SPLASH_RADIUS_1_5, 0);
    assert(mutt->state == TH_STATE_DEAD && (mutt->flags & TH_FLAG_GIB) && gs.things.kills == 1);
    bast_map_free(m);
}

static void t_splash_wall(void)
{
    bast_map *m = open_map();
    const int32_t open = splash_on_woofu(m, FX(5) - 13107, FX(6) + 13107); /* 4.8 -> 6.2 */
    bast_map_cell_at(m, 5, 3)->flags = BAST_CELL_SOLID;
    const int32_t walled = splash_on_woofu(m, FX(5) - 13107, FX(6) + 13107);
    printf("proj_splash_wall: d=1.4 open -> %d, behind a wall -> %d\n", open, walled);
    assert(open > 0);
    assert(walled == 0);
    bast_map_free(m);
}

static void t_self(void)
{
    bast_map *m = open_map();
    game_state gs;
    setup(&gs, m, FX(1) + 32768, 32768); /* 0.5 u from the west wall, facing it */
    game_tic(&gs, &FIRE);
    for (int k = 0; k < 5; k++)
        game_tic(&gs, &IDLE);
    printf("proj_self: point-blank scarab into a wall, health 100 -> %d\n", gs.player.health);
    assert(find(&gs, TH_TYPE_EXPLOSION_SMALL));
    assert(gs.player.health < 100 && gs.player.health > 0);
    /* Never hits its owner: a scarab spawned on the player flies off. */
    setup(&gs, m, FX(10), 0);
    game_tic(&gs, &FIRE);
    game_tic(&gs, &IDLE);
    assert(find(&gs, TH_TYPE_SCARAB_PROJ) && gs.player.health == 100);
    bast_map_free(m);
}

static void t_direct(void)
{
    /* A direct hit on a Woofu: direct 30..50, plus the splash at the
     * distance from the impact point to its centre. */
    bast_map *m = open_map();
    game_state gs;
    setup(&gs, m, FX(2) + HALF, 0);
    thinker_t *w = thinker_get(&gs.things, thinker_spawn(&gs.things, BAST_ENT_WOOFU, FX(10), FX(3) + HALF, 0));
    game_tic(&gs, &FIRE);
    thinker_t *s = find(&gs, TH_TYPE_SCARAB_PROJ);
    while (s->type == TH_TYPE_SCARAB_PROJ)
        game_tic(&gs, &IDLE);
    const fix_t d = w->x - s->x;
    const int32_t splash = (int32_t)((int64_t)96 * (SPLASH_RADIUS_1_5 - d) / SPLASH_RADIUS_1_5);
    const int32_t dmg = 2400 - w->health;
    printf("proj_direct: hit at d=%d, damage %d (splash part %d)\n", d, dmg, splash);
    assert(d < w->radius + s->radius);
    assert(dmg >= 30 + splash && dmg <= 50 + splash);
    bast_map_free(m);
}

static void t_barrel_chain(void)
{
    bast_map *m = open_map();
    /* Three barrels 1.0 u apart: 64 * (1.5 - 1.0) / 1.5 = 21 >= 20 HP;
     * the third is 2.0 u from the first, out of its reach. */
    add_entity(m, BAST_ENT_KIBBLE_BARREL, FX(10) + HALF, FX(3) + HALF);
    add_entity(m, BAST_ENT_KIBBLE_BARREL, FX(11) + HALF, FX(3) + HALF);
    add_entity(m, BAST_ENT_KIBBLE_BARREL, FX(12) + HALF, FX(3) + HALF);
    game_state gs;
    setup(&gs, m, FX(2) + HALF, 0);
    assert(gs.things.count == 3 && gs.things.kill_total == 0);
    thinker_t *b[3];
    for (int i = 0; i < 3; i++) {
        b[i] = thinker_get(&gs.things, (uint16_t)(i + 1));
        assert(b[i]->type == BAST_ENT_KIBBLE_BARREL && b[i]->health == BARREL_HP);
        assert(b[i]->radius == BARREL_RADIUS && thinker_alive(b[i]));
    }
    /* Shoot the first with the pistol (hitscan, in weapon_tic). */
    gs.player.weapons.ready = WP_PISTOL;
    uint32_t kill_tic = 0;
    for (int k = 0; k < 60 && !kill_tic; k++) {
        const uint32_t tic = gs.tic;
        game_tic(&gs, &FIRE);
        if (b[0]->state == TH_STATE_DYING)
            kill_tic = tic;
    }
    assert(kill_tic && b[1]->state == TH_STATE_STAND);
    uint32_t boom[3] = { 0, 0, 0 };
    for (int k = 0; k < 20; k++) {
        const uint32_t tic = gs.tic;
        game_tic(&gs, &IDLE);
        for (int i = 0; i < 3; i++)
            if (!boom[i] && b[i]->state == TH_STATE_DEAD)
                boom[i] = tic;
    }
    printf("proj_barrel_chain: hit on tic %u, explosions on tics %u %u %u\n", kill_tic, boom[0],
           boom[1], boom[2]);
    assert(boom[0] == kill_tic + 4);
    assert(boom[1] == boom[0] + 4);
    assert(boom[2] == boom[1] + 4);
    assert(gs.things.kills == 0);
    int fx = 0;
    for (int i = 0; i < gs.things.count; i++)
        fx += gs.things.list[i].type == TH_TYPE_EXPLOSION_BIG;
    assert(fx == 3);
    bast_map_free(m);
}

static void t_barrel_solid(void)
{
    bast_map *m = open_map();
    add_entity(m, BAST_ENT_KIBBLE_BARREL, FX(5) + HALF, FX(3) + HALF);
    game_state gs;
    setup(&gs, m, FX(2) + HALF, 0);
    const ticcmd walk = { 127, 0, 0, 0 };
    for (int k = 0; k < 70; k++)
        game_tic(&gs, &walk);
    printf("proj_barrel_solid: player stopped at x=%d (barrel %d, radii %d)\n", gs.player.x,
           FX(5) + HALF, PLAYER_RADIUS + BARREL_RADIUS);
    assert(gs.player.x <= FX(5) + HALF - PLAYER_RADIUS - BARREL_RADIUS);
    assert(gs.player.x >= FX(5) + HALF - PLAYER_RADIUS - BARREL_RADIUS - 2);
    bast_map_free(m);
}

/* Awake enemies (#117 states) are hit: a scarab flies at a Chihuahua
 * Chomper in CHASE and stops on it (direct hit), and the splash reaches a
 * second chasing Chomper 0.75 u north of it. */
static void t_awake(void)
{
    bast_map *m = open_map();
    add_entity(m, BAST_ENT_CHIHUAHUA_CHOMPER, FX(9) + HALF, FX(3) + HALF);
    add_entity(m, BAST_ENT_CHIHUAHUA_CHOMPER, FX(9) + HALF, FX(4) + HALF / 2);
    game_state gs;
    setup(&gs, m, FX(2) + HALF, 0);
    thinker_t *a = thinker_get(&gs.things, 1), *b = thinker_get(&gs.things, 2);
    a->state = b->state = TH_STATE_CHASE;
    game_tic(&gs, &FIRE);
    thinker_t *s = find(&gs, TH_TYPE_SCARAB_PROJ);
    assert(s);
    uint8_t sa = 0, sb = 0;
    int tics = 0;
    while (s->type == TH_TYPE_SCARAB_PROJ && tics++ < 40) {
        sa = a->state;
        sb = b->state;
        game_tic(&gs, &IDLE);
    }
    assert(s->type == TH_TYPE_EXPLOSION_SMALL);
    const int64_t dx = (int64_t)a->x - s->x, dy = (int64_t)a->y - s->y;
    const int64_t r = (int64_t)a->radius + s->radius;
    printf("proj_awake: states before impact %d %d, scarab hit after %d tics, "
           "health %d %d, kills %d\n", sa, sb, tics, a->health, b->health, gs.things.kills);
    assert(sa == TH_STATE_CHASE && sb == TH_STATE_CHASE);
    assert(dx * dx + dy * dy < r * r);           /* stopped on the chasing Chomper */
    assert(a->state == TH_STATE_DEAD && b->state == TH_STATE_DEAD);
    assert(b->health < 15 && gs.things.kills == 2); /* b took splash only */
    bast_map_free(m);
}

/* Effects, dying barrels and free slots are not "alive": an explosion in a
 * doorway does not hold the door open, and AI never runs them. */
static void t_alive(void)
{
    bast_map *m = open_map();
    bast_map_cell_at(m, 10, 3)->flags = BAST_CELL_DOOR;
    add_entity(m, BAST_ENT_KIBBLE_BARREL, FX(20) + HALF, FX(3) + HALF);
    game_state gs;
    setup(&gs, m, FX(2) + HALF, 0);
    assert(gs.door_count == 1);
    thinker_t *barrel = thinker_get(&gs.things, 1);
    assert(thinker_alive(barrel));
    door_activate(&gs, 0);
    for (int k = 0; k < 40; k++)
        game_tic(&gs, &IDLE);
    assert(gs.doors[0].state == DOOR_OPEN);
    /* A long-lived explosion effect standing in the doorway. */
    thinker_t *fx = thinker_get(&gs.things, thinker_alloc(&gs.things));
    fx->type = TH_TYPE_EXPLOSION_BIG;
    fx->state = TH_STATE_STAND;
    fx->x = FX(10) + HALF;
    fx->y = FX(3) + HALF;
    fx->radius = BARREL_RADIUS;
    fx->tics = 100000;
    assert(!thinker_alive(fx));
    int closed_at = -1;
    for (int k = 0; k < DOOR_WAIT_TICS + 80 && closed_at < 0; k++) {
        game_tic(&gs, &IDLE);
        if (gs.doors[0].state == DOOR_CLOSED)
            closed_at = k;
    }
    printf("proj_alive: door closed %d tics after the effect entered it\n", closed_at);
    assert(closed_at >= 0 && fx->type == TH_TYPE_EXPLOSION_BIG && fx->x == FX(10) + HALF);
    /* A projectile, a dying barrel and a free slot are not alive either. */
    thinker_t probe = *fx;
    probe.type = TH_TYPE_SCARAB_PROJ;
    assert(!thinker_alive(&probe));
    probe = *barrel;
    probe.state = TH_STATE_DYING;
    assert(!thinker_alive(&probe));
    probe.state = TH_STATE_FREE;
    assert(!thinker_alive(&probe));
    /* Gunfire noise never wakes a barrel. */
    ai_noise(&gs);
    assert(barrel->state == TH_STATE_STAND);
    bast_map_free(m);
}

int main(int argc, char **argv)
{
    const char *g = argc > 1 ? argv[1] : "all";
    const int all = strcmp(g, "all") == 0;
    if (all || !strcmp(g, "travel")) t_travel();
    if (all || !strcmp(g, "wall")) t_wall();
    if (all || !strcmp(g, "splash")) t_splash();
    if (all || !strcmp(g, "splash_wall")) t_splash_wall();
    if (all || !strcmp(g, "self")) t_self();
    if (all || !strcmp(g, "direct")) t_direct();
    if (all || !strcmp(g, "barrel_chain")) t_barrel_chain();
    if (all || !strcmp(g, "barrel_solid")) t_barrel_solid();
    if (all || !strcmp(g, "awake")) t_awake();
    if (all || !strcmp(g, "alive")) t_alive();
    return 0;
}
