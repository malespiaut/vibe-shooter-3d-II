/* Mastiff Juggernaut and Shepherd Shieldbearer AI (enemies.md Stats,
 * Per-enemy notes; issue #96). Maps are built in memory with libbast.
 * Usage: test_ai_mastshep <group>. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "game/ai.h"
#include "game/enemy_info.h"
#include "game/projectile.h"
#include "game/sound.h"
#include "game/state.h"

#define FX(i) fix_from_int(i)
#define HALF (FIX_ONE / 2)
#define CENTRE(i) (FX(i) + HALF)

static const ticcmd IDLE = { 0, 0, 0, 0 };
static const ticcmd FIRE = { 0, 0, 0, BT_FIRE };

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

/* Player at the centre of (2, 4) facing east with `wp` ready, no thinkers. */
static void setup(game_state *gs, const bast_map *m, int wp)
{
    game_init(gs, m, 1, 2, 0);
    gs->things.count = 0;
    gs->things.kill_total = 0;
    gs->player.x = CENTRE(2);
    gs->player.y = CENTRE(4);
    gs->player.angle = 0;
    weapons_t *w = &gs->player.weapons;
    w->owned |= 1u << wp;
    w->ready = (uint8_t)wp;
    w->ammo[AM_CHARGES] = 100;
    w->ammo[AM_CELLS] = 300;
    w->ammo[AM_SCARABS] = 20;
}

static thinker_t *spawn(game_state *gs, uint16_t type, fix_t x, fix_t y, angle_t a)
{
    uint16_t id = thinker_spawn(&gs->things, type, x, y, a);
    assert(id);
    return thinker_get(&gs->things, id);
}

static thinker_t *find_type(game_state *gs, uint16_t type, int nth)
{
    for (int i = 0; i < gs->things.count; i++)
        if (gs->things.list[i].type == type && nth-- == 0)
            return &gs->things.list[i];
    return NULL;
}

static int queued(const game_state *gs, int sfx)
{
    for (int i = 0; i < gs->sounds.count; i++)
        if (gs->sounds.ev[i].id == sfx)
            return 1;
    return 0;
}

/* Wind-up 24 tics; missiles leave from alternating shoulders, 0.35 u left
 * then right of the centre line, and the shoulder flips each shot. */
static void test_mastiff_shoulders(void)
{
    bast_map *m = open_map(40, 9); /* long: no missile lands during the test */
    game_state gs;
    setup(&gs, m, WP_PISTOL);
    assert(ai_windup_tics(&gs, BAST_ENT_MASTIFF_JUGGERNAUT) == 24);
    thinker_t *t = spawn(&gs, BAST_ENT_MASTIFF_JUGGERNAUT, CENTRE(36), CENTRE(4), ANG180);
    for (int shot = 0; shot < 3; shot++) {
        t->state = TH_STATE_WINDUP;
        t->tics = ai_windup_tics(&gs, t->type);
        for (int i = 0; i < 23; i++)
            game_tic(&gs, &IDLE);
        assert(t->state == TH_STATE_WINDUP && !find_type(&gs, TH_TYPE_BONE_PROJ, shot));
        game_tic(&gs, &IDLE);
        const thinker_t *b = find_type(&gs, TH_TYPE_BONE_PROJ, shot);
        assert(b && t->state == TH_STATE_ATTACK && queued(&gs, SFX_bone_fire));
        const fix_t off = b->y - t->y;
        printf("shoulders: shot %d at x=%d y offset %d (owner %u)\n", shot, b->x, off, b->owner);
        /* Facing west: left is south (-y), right is north (+y). */
        assert(b->x == t->x);
        assert(off == (shot % 2 == 0 ? -22938 : 22938));
        assert(b->owner == (uint16_t)(t - gs.things.list + 1));
        /* Aimed at the player from the launch point: heading west. */
        assert(b->momx < 0);
        assert(b->momy == 0 || (off < 0) == (b->momy > 0));
        gs.player.health = 100;
        t->state = TH_STATE_CHASE;
    }
    bast_map_free(m);
}

/* A bone missile flies exactly 11235 per tic: dead east it moves
 * (11235, 0) a tic; one the Mastiff fires has |velocity| 11235 +-1. */
static void test_mastiff_speed(void)
{
    bast_map *m = open_map(30, 9);
    game_state gs;
    setup(&gs, m, WP_PISTOL);
    const uint16_t id = projectile_spawn(&gs, TH_TYPE_BONE_PROJ, TH_ID_PLAYER, CENTRE(4),
                                         CENTRE(6), AI_MISSILE_Z, 0);
    thinker_t *b = thinker_get(&gs.things, id);
    assert(b && b->momx == 11235 && b->momy == 0);
    const fix_t x0 = b->x;
    for (int i = 1; i <= 20; i++) {
        game_tic(&gs, &IDLE);
        assert(b->x == x0 + 11235 * i && b->y == CENTRE(6));
    }
    printf("speed: 20 tics moved %d (= 20 x 11235)\n", b->x - x0);

    setup(&gs, m, WP_PISTOL);
    thinker_t *t = spawn(&gs, BAST_ENT_MASTIFF_JUGGERNAUT, CENTRE(14), CENTRE(6), ANG180);
    t->state = TH_STATE_WINDUP;
    t->tics = 1;
    game_tic(&gs, &IDLE);
    b = find_type(&gs, TH_TYPE_BONE_PROJ, 0);
    assert(b);
    const fix_t v = fix_dist(b->momx, b->momy);
    printf("speed: fired missile |v| = %d\n", v);
    assert(v >= 11234 && v <= 11236);
    bast_map_free(m);
}

/* Lance at minimum rolls: 4 x 100 = 400 kills a Mastiff, 3 do not. */
static void test_mastiff_lance(void)
{
    bast_map *m = open_map(20, 9);
    game_state gs;
    setup(&gs, m, WP_LANCE);
    assert(weapon_info[WP_LANCE].dmg_base == 100);
    thinker_t *t = spawn(&gs, BAST_ENT_MASTIFF_JUGGERNAUT, CENTRE(8), CENTRE(4), ANG180);
    assert(t->health == 400);
    for (int i = 0; i < 3; i++)
        assert(ai_damage_from(&gs, t, 100, DMG_DIRECT, gs.player.x, gs.player.y) == 0);
    assert(t->health == 100 && thinker_alive(t));
    assert(ai_damage_from(&gs, t, 100, DMG_DIRECT, gs.player.x, gs.player.y) == 1);
    printf("lance: 4 x 100 -> health %d, state %d, kills %d\n", t->health, t->state,
           gs.things.kills);
    assert(t->health == 0 && t->state == TH_STATE_DEAD && gs.things.kills == 1);

    /* Real beams: 100..140 each, so 3 or 4 beams. */
    setup(&gs, m, WP_LANCE);
    gs.player.health = 1000; /* its missiles must not end the test */
    t = spawn(&gs, BAST_ENT_MASTIFF_JUGGERNAUT, CENTRE(8), CENTRE(4), ANG180);
    int tics = 0;
    while (thinker_alive(t) && tics < 400) {
        game_tic(&gs, &FIRE);
        tics++;
    }
    printf("lance: real beams killed it after %u shots\n", gs.player.weapons.shots);
    assert(!thinker_alive(t) && gs.player.weapons.shots >= 3 && gs.player.weapons.shots <= 4);
    bast_map_free(m);
}

/* A bone missile that reaches the player deals 20..40 direct plus splash
 * (32 at most): the player loses between 20 and 72. */
static void test_mastiff_hit(void)
{
    bast_map *m = open_map(20, 9);
    game_state gs;
    setup(&gs, m, WP_PISTOL);
    gs.player.health = 200;
    thinker_t *t = spawn(&gs, BAST_ENT_MASTIFF_JUGGERNAUT, CENTRE(8), CENTRE(4), ANG180);
    t->state = TH_STATE_WINDUP;
    t->tics = 1;
    int tics = 0, boom = 0;
    while (gs.player.health == 200 && tics++ < 100) {
        game_tic(&gs, &IDLE);
        boom |= queued(&gs, SFX_bone_explode);
    }
    const int lost = 200 - gs.player.health;
    printf("hit: player lost %d after %d tics\n", lost, tics);
    assert(boom && lost >= 20 && lost <= 72);
    bast_map_free(m);
}

/* Pistol from the front: 0 damage and shield_block; from 90 degrees off
 * the facing: damage. */
static void test_shep_front(void)
{
    bast_map *m = open_map(20, 9);
    game_state gs;
    setup(&gs, m, WP_PISTOL);
    thinker_t *t = spawn(&gs, BAST_ENT_SHEPHERD_SHIELDBEARER, CENTRE(8), CENTRE(4), ANG180);
    assert(ai_shield_up(t));
    game_tic(&gs, &FIRE);
    printf("front: health %d, block sound %d, state %d\n", t->health,
           queued(&gs, SFX_shield_block), t->state);
    assert(gs.player.weapons.shots == 1 && t->health == 90 && queued(&gs, SFX_shield_block));
    assert(t->state != TH_STATE_STAND); /* the blocked shot woke it */

    setup(&gs, m, WP_PISTOL);
    t = spawn(&gs, BAST_ENT_SHEPHERD_SHIELDBEARER, CENTRE(8), CENTRE(4), ANG90); /* faces north */
    game_tic(&gs, &FIRE);
    printf("side (90 deg): health %d\n", t->health);
    assert(t->health < 90 && !queued(&gs, SFX_shield_block));

    /* Edge of the arc: 59 degrees blocked, 61 degrees not. */
    setup(&gs, m, WP_PISTOL);
    t = spawn(&gs, BAST_ENT_SHEPHERD_SHIELDBEARER, CENTRE(8), CENTRE(4),
              (angle_t)(ANG180 + 59 * 65536 / 360));
    assert(ai_damage_from(&gs, t, 10, DMG_DIRECT, gs.player.x, gs.player.y) == 0 && t->health == 90);
    t->angle = (angle_t)(ANG180 + 61 * 65536 / 360);
    ai_damage_from(&gs, t, 10, DMG_DIRECT, gs.player.x, gs.player.y);
    assert(t->health == 80);
    bast_map_free(m);
}

/* Splash from in front passes at half, from behind in full; a scarab into
 * the shield deals only half its splash (the direct hit is blocked). */
static void test_shep_splash(void)
{
    bast_map *m = open_map(20, 9);
    game_state gs;
    setup(&gs, m, WP_LAUNCHER);
    thinker_t *t = spawn(&gs, BAST_ENT_SHEPHERD_SHIELDBEARER, CENTRE(8), CENTRE(4), ANG180);
    /* d = 0.5 u: 96 * 1.0 / 1.5 = 64 full, 32 half. */
    splash_damage(&gs, t->x - HALF, t->y, 96, SPLASH_RADIUS_1_5, 0);
    printf("splash: front %d\n", 90 - t->health);
    assert(t->health == 90 - 32);
    t->health = 90;
    splash_damage(&gs, t->x + HALF, t->y, 96, SPLASH_RADIUS_1_5, 0);
    printf("splash: behind %d\n", 90 - t->health);
    assert(t->health == 90 - 64);

    setup(&gs, m, WP_LAUNCHER);
    t = spawn(&gs, BAST_ENT_SHEPHERD_SHIELDBEARER, CENTRE(8), CENTRE(4), ANG180);
    t->flags |= TH_FLAG_AMBUSH; /* stays put: no route change while the scarab flies */
    game_tic(&gs, &FIRE);
    int tics = 0;
    while (find_type(&gs, TH_TYPE_SCARAB_PROJ, 0) && tics++ < 60)
        game_tic(&gs, &IDLE);
    const int dmg = 90 - t->health;
    printf("splash: scarab into the shield dealt %d (splash 96 halved, no direct)\n", dmg);
    assert(dmg > 0 && dmg <= 48);
    bast_map_free(m);
}

/* Howler from the front: 80 in full, and the shield stays down for the
 * next 70 tics; a pistol shot from the front 34 tics later hurts. */
static void test_shep_howler(void)
{
    bast_map *m = open_map(20, 9);
    game_state gs;
    setup(&gs, m, WP_HOWLER);
    gs.player.weapons.owned |= 1u << WP_PISTOL;
    thinker_t *t = spawn(&gs, BAST_ENT_SHEPHERD_SHIELDBEARER, CENTRE(5), CENTRE(4), ANG180);
    t->health = t->max_health = 1000; /* survive every test shot */
    game_tic(&gs, &FIRE);
    printf("howler: took %d, shield_tics %d, up %d\n", 1000 - t->health, t->shield_tics,
           ai_shield_up(t));
    assert(t->health == 920 && t->shield_tics == AI_SHIELD_DROP_TICS && !ai_shield_up(t));
    assert(!t->howler_hit);
    for (int k = 1; k <= 34; k++) {
        game_tic(&gs, &IDLE);
        assert(t->shield_tics == AI_SHIELD_DROP_TICS - k && !ai_shield_up(t));
    }
    /* Still in the Howler's 35-tic pain, facing the player: pistol. */
    gs.player.weapons.ready = WP_PISTOL;
    gs.player.weapons.refire_tics = 0;
    const int32_t before = t->health;
    game_tic(&gs, &FIRE);
    printf("howler: pistol from the front at +35 tics took %d\n", before - t->health);
    assert(t->health < before && !queued(&gs, SFX_shield_block));
    for (int k = 36; k <= 70; k++) {
        game_tic(&gs, &IDLE);
        assert(t->shield_tics == AI_SHIELD_DROP_TICS - k);
    }
    printf("howler: shield_tics 0 after 70 tics, up %d (state %d)\n", ai_shield_up(t), t->state);
    assert(t->shield_tics == 0);
    assert(ai_shield_up(t) == (t->state != TH_STATE_WINDUP && t->state != TH_STATE_ATTACK));
    bast_map_free(m);
}

/* Wind-up: the shield swings aside, a pistol shot from the front hurts;
 * the baton then hits 12..20 when the player is within 1.0 u. */
static void test_shep_windup(void)
{
    bast_map *m = open_map(20, 9);
    game_state gs;
    setup(&gs, m, WP_PISTOL);
    assert(ai_windup_tics(&gs, BAST_ENT_SHEPHERD_SHIELDBEARER) == 16);
    thinker_t *t = spawn(&gs, BAST_ENT_SHEPHERD_SHIELDBEARER, CENTRE(3) + HALF / 2, CENTRE(4),
                         ANG180); /* 1.25 u away: near edge 1.0 u, in reach */
    t->health = t->max_health = 1000;
    t->state = TH_STATE_CHASE;
    gs.player.health = 200;
    game_tic(&gs, &IDLE); /* in reach: winds up */
    assert(t->state == TH_STATE_WINDUP && !ai_shield_up(t) && queued(&gs, SFX_shep_windup));
    game_tic(&gs, &FIRE);
    printf("windup: pistol from the front took %d\n", 1000 - t->health);
    assert(t->health < 1000);
    /* The forced pain roll may cancel the wind-up; re-arm it. */
    t->state = TH_STATE_WINDUP;
    t->tics = 1;
    game_tic(&gs, &IDLE);
    const int lost = 200 - gs.player.health;
    printf("windup: baton dealt %d\n", lost);
    assert(lost >= 12 && lost <= 20 && queued(&gs, SFX_baton_hit));
    assert(t->state == TH_STATE_ATTACK && !ai_shield_up(t));
    bast_map_free(m);
}

int main(int argc, char **argv)
{
    static const struct {
        const char *name;
        void (*fn)(void);
    } groups[] = {
        { "mastiff_shoulders", test_mastiff_shoulders },
        { "mastiff_speed", test_mastiff_speed },
        { "mastiff_lance", test_mastiff_lance },
        { "mastiff_hit", test_mastiff_hit },
        { "shep_front", test_shep_front },
        { "shep_splash", test_shep_splash },
        { "shep_howler", test_shep_howler },
        { "shep_windup", test_shep_windup },
    };
    for (size_t i = 0; i < sizeof groups / sizeof groups[0]; i++) {
        if (argc < 2 || strcmp(argv[1], groups[i].name) == 0) {
            groups[i].fn();
            if (argc >= 2)
                return 0;
        }
    }
    if (argc >= 2) {
        fprintf(stderr, "test_ai_mastshep: unknown group %s\n", argv[1]);
        return 1;
    }
    return 0;
}
