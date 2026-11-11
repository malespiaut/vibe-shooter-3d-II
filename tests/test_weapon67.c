/* Weapons 6-7 (weapons.md Stats, Behaviour notes): Eye of Ra Lance
 * (piercing beam) and Sphinx Howler (cone, knockback, forced pain). Maps
 * are built in memory with libbast. Usage: test_weapon67 <group>. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "game/ai.h"
#include "game/sound.h"
#include "game/state.h"

#define FX(i) fix_from_int(i)
#define HALF (FIX_ONE / 2)
#define CENTRE(i) (FX(i) + HALF)
#define DEG(d) ((angle_t)((d) * 65536 / 360))

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

/* Player at the centre of (2, 4) facing east, `wp` in hand, 300 cells, no
 * thinkers. */
static void setup(game_state *gs, const bast_map *m, int wp)
{
    game_init(gs, m, 1, 2, 0);
    gs->things.count = 0;
    gs->player.x = CENTRE(2);
    gs->player.y = CENTRE(4);
    gs->player.angle = 0;
    weapons_t *w = &gs->player.weapons;
    w->owned |= 1u << wp;
    w->ready = (uint8_t)wp;
    w->ammo[AM_CELLS] = 300;
}

static thinker_t *spawn(game_state *gs, uint16_t type, fix_t x, fix_t y)
{
    uint16_t id = thinker_spawn(&gs->things, type, x, y, ANG180);
    assert(id);
    return thinker_get(&gs->things, id);
}

/* A thinker at distance `d` from the player, `deg` degrees off east. */
static thinker_t *spawn_polar(game_state *gs, uint16_t type, fix_t d, int deg)
{
    const angle_t a = DEG(deg);
    return spawn(gs, type, gs->player.x + fix_mul(d, fine_cos(a)),
                 gs->player.y + fix_mul(d, fine_sin(a)));
}

static int hurt(const thinker_t *t)
{
    return t->health < t->max_health;
}

/* Five Mastiffs (400 HP, they survive one beam) in a line east: the 4
 * nearest take 100..140 each, the 5th none. Listed far-to-near so the
 * order comes from the distance sort, not creation order. */
static void test_pierce(void)
{
    bast_map *m = open_map(20, 9);
    game_state gs;
    setup(&gs, m, WP_LANCE);
    for (int i = 4; i >= 0; i--)
        spawn(&gs, BAST_ENT_MASTIFF_JUGGERNAUT, CENTRE(4 + 2 * i), CENTRE(4));
    game_tic(&gs, &FIRE);
    for (int i = 0; i < 5; i++) {
        const thinker_t *t = &gs.things.list[i];
        const int32_t dmg = t->max_health - t->health;
        printf("pierce: mastiff at x=%d.5 took %d\n", fix_to_int(t->x), dmg);
        if (i == 0) /* the farthest (x = 12.5) */
            assert(dmg == 0);
        else
            assert(dmg >= 100 && dmg <= 140);
    }
    assert(gs.player.weapons.ammo[AM_CELLS] == 295);
    assert(gs.player.weapons.flash_tics == WEAPON_FLASH_TICS);
    assert(gs.player.weapons.refire_tics == 40);

    /* Interval 40: 9 shots in 350 tics. */
    setup(&gs, m, WP_LANCE);
    for (int i = 0; i < 350; i++)
        game_tic(&gs, &FIRE);
    printf("pierce: %u shots in 350 tics\n", gs.player.weapons.shots);
    assert(gs.player.weapons.shots == 9);
    bast_map_free(m);
}

/* A wall column at x = 8: the Mastiff before it is hit, the one behind it
 * is not. */
static void test_wall(void)
{
    bast_map *m = open_map(20, 9);
    for (int y = 1; y < 8; y++)
        bast_map_cell_at(m, 8, y)->flags = BAST_CELL_SOLID;
    game_state gs;
    setup(&gs, m, WP_LANCE);
    thinker_t *near = spawn(&gs, BAST_ENT_MASTIFF_JUGGERNAUT, CENTRE(5), CENTRE(4));
    thinker_t *far = spawn(&gs, BAST_ENT_MASTIFF_JUGGERNAUT, CENTRE(11), CENTRE(4));
    game_tic(&gs, &FIRE);
    printf("wall: before %d, behind %d\n", near->max_health - near->health,
           far->max_health - far->health);
    assert(hurt(near) && !hurt(far));
    bast_map_free(m);
}

/* Cone: 4.9 u dead ahead and 29 degrees off are hit; 5.1 u, 31 degrees,
 * behind the player and behind a pillar are not. */
static void test_cone(void)
{
    bast_map *m = open_map(20, 11);
    bast_map_cell_at(m, 5, 3)->flags = BAST_CELL_SOLID; /* pillar */
    game_state gs;
    setup(&gs, m, WP_HOWLER);
    const uint16_t M = BAST_ENT_MASTIFF_JUGGERNAUT;
    thinker_t *in49 = spawn_polar(&gs, M, 321126, 0);           /* 4.9 u */
    thinker_t *out51 = spawn_polar(&gs, M, 334234, 10);         /* 5.1 u */
    thinker_t *in29 = spawn_polar(&gs, M, FX(3), 29);
    thinker_t *out31 = spawn_polar(&gs, M, FX(3), -31);
    thinker_t *behind = spawn_polar(&gs, M, FX(2), 180);
    thinker_t *pillar = spawn(&gs, M, CENTRE(6), CENTRE(3)); /* line crosses tile (5,3) */
    assert(fix_dist(in49->x - gs.player.x, in49->y - gs.player.y) <= FX(5));
    assert(fix_dist(out51->x - gs.player.x, out51->y - gs.player.y) > FX(5));
    assert(!ai_line_of_sight(&gs, pillar));
    game_tic(&gs, &FIRE);
    printf("cone: 4.9u %d, 5.1u %d, 29deg %d, 31deg %d, behind %d, pillar %d\n", hurt(in49),
           hurt(out51), hurt(in29), hurt(out31), hurt(behind), hurt(pillar));
    assert(hurt(in49) && in49->max_health - in49->health == 80 && in49->howler_hit);
    assert(hurt(in29) && in29->howler_hit);
    assert(!hurt(out51) && !hurt(out31) && !hurt(behind) && !hurt(pillar));
    assert(!out51->howler_hit && !out31->howler_hit && !behind->howler_hit && !pillar->howler_hit);
    assert(gs.player.weapons.ammo[AM_CELLS] == 275);
    assert(gs.player.weapons.flash_tics == 0); /* no muzzle flash on the Howler */
    bast_map_free(m);
}

/* Knockback: 1 u (65536) straight away in the open; less against a wall,
 * and never into it, head-on or at an angle into a corner. */
static void test_knockback(void)
{
    bast_map *m = open_map(20, 9);
    for (int y = 1; y < 8; y++)
        bast_map_cell_at(m, 8, y)->flags = BAST_CELL_SOLID; /* wall column x = 8 */
    game_state gs;
    setup(&gs, m, WP_HOWLER);
    thinker_t *t = spawn(&gs, BAST_ENT_MASTIFF_JUGGERNAUT, CENTRE(4), CENTRE(4));
    const fix_t x0 = t->x, y0 = t->y;
    game_tic(&gs, &FIRE);
    printf("knockback: open %d units\n", t->x - x0);
    assert(t->x - x0 == 65536 && t->y == y0);

    /* 0.7 u in front of the wall (radius 0.45): it stops short of x = 8. */
    setup(&gs, m, WP_HOWLER);
    t = spawn(&gs, BAST_ENT_MASTIFF_JUGGERNAUT, FX(7) + 19661, CENTRE(4)); /* x = 7.3 */
    const fix_t x1 = t->x;
    game_tic(&gs, &FIRE);
    printf("knockback: wall %d units, right edge %d (wall at %d)\n", t->x - x1, t->x + t->radius,
           FX(8));
    assert(t->x - x1 < 65536 && t->x > x1);
    assert(t->x + t->radius <= FX(8));
    assert(!ai_circle_blocked(&gs, t->x, t->y, t->radius));

    /* Into the north-east corner of the room x 1..7, y 1..7, at 45 deg. */
    setup(&gs, m, WP_HOWLER);
    gs.player.x = FX(5);
    gs.player.y = FX(5);
    gs.player.angle = ANG45;
    t = spawn(&gs, BAST_ENT_MASTIFF_JUGGERNAUT, FX(6) + 26214, FX(6) + 26214); /* 6.4, 6.4 */
    game_tic(&gs, &FIRE);
    printf("knockback: corner at %d,%d\n", t->x, t->y);
    assert(hurt(t));
    assert(t->x + t->radius <= FX(8) && t->y + t->radius <= FX(8));
    assert(!ai_circle_blocked(&gs, t->x, t->y, t->radius));

    /* A thin 1-tile wall at x = 8 never lets a small Chomper (0.1875) through. */
    setup(&gs, m, WP_HOWLER);
    gs.player.x = FX(7) + 13107; /* 7.2 */
    t = spawn(&gs, BAST_ENT_CHIHUAHUA_CHOMPER, FX(7) + 52428, CENTRE(4)); /* 7.8 */
    t->health = t->max_health = 1000;
    game_tic(&gs, &FIRE);
    printf("knockback: chomper at %d (wall from %d)\n", t->x, FX(8));
    assert(t->x + t->radius <= FX(8));
    bast_map_free(m);
}

/* Forced pain: 35 tics in PAIN whatever the pain chance, then CHASE. */
static void test_pain(void)
{
    bast_map *m = open_map(20, 9);
    for (uint32_t seed = 0; seed < 8; seed++) {
        game_state gs;
        setup(&gs, m, WP_HOWLER);
        gs.rng.state = seed * 2654435761u + 1;
        thinker_t *t = spawn(&gs, BAST_ENT_MASTIFF_JUGGERNAUT, CENTRE(5), CENTRE(4));
        /* Fire precedes ai_tic, so the fire tic is the first of the 35 AI
         * tics spent flinching; the 35th returns it to CHASE. */
        game_tic(&gs, &FIRE);
        int pain_tics = 1;
        const fix_t x0 = t->x, y0 = t->y;
        while (t->state == TH_STATE_PAIN && pain_tics < 100) {
            game_tic(&gs, &IDLE);
            pain_tics++;
            assert(t->state == TH_STATE_CHASE || (t->x == x0 && t->y == y0)); /* stays put */
        }
        if (seed == 0 || pain_tics != HOWLER_PAIN_TICS)
            printf("pain: %d tics from the shot to CHASE (state %d)\n", pain_tics, t->state);
        fflush(stdout);
        assert(pain_tics == HOWLER_PAIN_TICS);
        assert(t->state == TH_STATE_CHASE);
    }
    bast_map_free(m);
}

/* Point-blank Howler on a ring of Chompers: the player takes nothing (it
 * never hurts the player, and they are all flinching). */
static void test_self(void)
{
    bast_map *m = open_map(20, 9);
    game_state gs;
    setup(&gs, m, WP_HOWLER);
    for (int d = -20; d <= 20; d += 10) {
        thinker_t *t = spawn_polar(&gs, BAST_ENT_CHIHUAHUA_CHOMPER, 32768, d);
        t->health = t->max_health = 1000;
    }
    game_tic(&gs, &FIRE);
    printf("self: player health %d after point-blank Howler\n", gs.player.health);
    assert(gs.player.health == PLAYER_START_HEALTH);
    for (int i = 0; i < gs.things.count; i++)
        assert(gs.things.list[i].state == TH_STATE_PAIN);
    for (int i = 0; i < 30; i++)
        game_tic(&gs, &IDLE);
    assert(gs.player.health == PLAYER_START_HEALTH);
    bast_map_free(m);
}

/* Runs IDLE tics until `b` leaves DYING; returns how many (fuse check). */
static int fuse_tics(game_state *gs, const thinker_t *b)
{
    int n = 0;
    while (b->state == TH_STATE_DYING && n < 50) {
        game_tic(gs, &IDLE);
        n++;
    }
    return n;
}

/* Lance through a line with a kibble barrel second of five: the barrel
 * takes the beam, enters DYING and uses one of the 4 pierce slots, so the
 * Mastiffs 1, 3 and 4 are hit and the 5th is not. 4 tics later it
 * explodes through #93's normal fuse. */
static void test_barrel_lance(void)
{
    bast_map *m = open_map(20, 9);
    game_state gs;
    setup(&gs, m, WP_LANCE);
    const uint16_t M = BAST_ENT_MASTIFF_JUGGERNAUT;
    thinker_t *mast[4];
    mast[0] = spawn(&gs, M, CENTRE(4), CENTRE(4));
    thinker_t *b = thinker_get(&gs.things, thinker_spawn_barrel(&gs.things, CENTRE(6), CENTRE(4)));
    assert(b && b->type == BAST_ENT_KIBBLE_BARREL);
    mast[1] = spawn(&gs, M, CENTRE(9), CENTRE(4));
    mast[2] = spawn(&gs, M, CENTRE(11), CENTRE(4));
    mast[3] = spawn(&gs, M, CENTRE(13), CENTRE(4));
    game_tic(&gs, &FIRE);
    printf("barrel_lance: barrel hp %d state %d; mastiffs took %d %d %d %d\n", b->health,
           b->state, mast[0]->max_health - mast[0]->health, mast[1]->max_health - mast[1]->health,
           mast[2]->max_health - mast[2]->health, mast[3]->max_health - mast[3]->health);
    assert(b->state == TH_STATE_DYING && b->health <= 0);
    assert(hurt(mast[0]) && hurt(mast[1]) && hurt(mast[2]));
    assert(!hurt(mast[3])); /* slot 4 went to the barrel */
    const int fuse = fuse_tics(&gs, b);
    printf("barrel_lance: exploded %d tics after the fire tic\n", fuse);
    assert(b->state == TH_STATE_DEAD && fuse == BARREL_FUSE_TICS);
    assert(gs.things.kills == 0);
    bast_map_free(m);
}

/* Howler on a barrel in the cone: killed (80 >= 20 HP), normal fuse, never
 * PAIN, never pushed. A barrel that survives the hit (HP raised to 1000)
 * still neither flinches nor moves: the forced-pain branch is enemy-only. */
static void test_barrel_howler(void)
{
    bast_map *m = open_map(20, 9);
    game_state gs;
    setup(&gs, m, WP_HOWLER);
    thinker_t *b = thinker_get(&gs.things, thinker_spawn_barrel(&gs.things, CENTRE(4), CENTRE(4)));
    thinker_t *t = spawn(&gs, BAST_ENT_MASTIFF_JUGGERNAUT, CENTRE(6), CENTRE(5));
    const fix_t bx = b->x, by = b->y;
    game_tic(&gs, &FIRE);
    printf("barrel_howler: barrel hp %d state %d at %d,%d (was %d,%d); mastiff state %d\n",
           b->health, b->state, b->x, b->y, bx, by, t->state);
    assert(b->state == TH_STATE_DYING);
    assert(b->x == bx && b->y == by);
    assert(t->state == TH_STATE_PAIN); /* the enemy beside it still flinches */
    const int fuse = fuse_tics(&gs, b);
    printf("barrel_howler: exploded %d tics after the fire tic\n", fuse);
    assert(b->state == TH_STATE_DEAD && fuse == BARREL_FUSE_TICS);

    setup(&gs, m, WP_HOWLER);
    b = thinker_get(&gs.things, thinker_spawn_barrel(&gs.things, CENTRE(4), CENTRE(4)));
    b->health = b->max_health = 1000;
    game_tic(&gs, &FIRE);
    printf("barrel_howler: tough barrel hp %d state %d, moved %d\n", b->health, b->state,
           b->x - bx);
    assert(b->health == 920 && b->state == TH_STATE_STAND);
    assert(b->x == bx && b->y == by);
    bast_map_free(m);
}

/* Fire sounds: Lance queues lance_charge then lance_fire, Howler
 * howler_roar, all local, on the fire tic. */
static void test_sound(void)
{
    bast_map *m = open_map(20, 9);
    game_state gs;
    setup(&gs, m, WP_LANCE);
    game_tic(&gs, &FIRE);
    printf("sound: lance tic queued");
    for (int i = 0; i < gs.sounds.count; i++)
        printf(" %s", sfx_name(gs.sounds.ev[i].id));
    printf("\n");
    assert(gs.sounds.count == 2);
    assert(gs.sounds.ev[0].id == SFX_lance_charge && gs.sounds.ev[0].local);
    assert(gs.sounds.ev[1].id == SFX_lance_fire && gs.sounds.ev[1].local);

    setup(&gs, m, WP_HOWLER);
    game_tic(&gs, &FIRE);
    printf("sound: howler tic queued");
    for (int i = 0; i < gs.sounds.count; i++)
        printf(" %s", sfx_name(gs.sounds.ev[i].id));
    printf("\n");
    assert(gs.sounds.count == 1);
    assert(gs.sounds.ev[0].id == SFX_howler_roar && gs.sounds.ev[0].local);
    bast_map_free(m);
}

int main(int argc, char **argv)
{
    static const struct {
        const char *name;
        void (*fn)(void);
    } groups[] = {
        { "pierce", test_pierce }, { "wall", test_wall }, { "cone", test_cone },
        { "knockback", test_knockback }, { "pain", test_pain }, { "self", test_self },
        { "barrel_lance", test_barrel_lance }, { "barrel_howler", test_barrel_howler },
        { "sound", test_sound },
    };
    for (size_t i = 0; i < sizeof groups / sizeof groups[0]; i++) {
        if (argc < 2 || strcmp(argv[1], groups[i].name) == 0) {
            groups[i].fn();
            if (argc >= 2)
                return 0;
        }
    }
    if (argc >= 2) {
        fprintf(stderr, "test_weapon67: unknown group %s\n", argv[1]);
        return 1;
    }
    return 0;
}
