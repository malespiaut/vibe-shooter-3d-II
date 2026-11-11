/* Woofu boss AI (enemies.md Boss, game/boss.h, issue #97). Maps are built
 * in memory with libbast. Usage: test_boss <group>. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "game/ai.h"
#include "game/boss.h"
#include "game/enemy_info.h"
#include "game/projectile.h"
#include "game/sound.h"
#include "game/state.h"
#include "game/tags.h"

#define FX(i) fix_from_int(i)
#define HALF (FIX_ONE / 2)
#define CENTRE(i) (FX(i) + HALF)

static const ticcmd IDLE = { 0, 0, 0, 0 };

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

static int add_entity(bast_map *m, uint16_t type, int tx, int ty, angle_t angle, uint8_t tag)
{
    int i = bast_map_add_entity(m);
    assert(i >= 0);
    m->entities[i].type = type;
    m->entities[i].x = CENTRE(tx);
    m->entities[i].y = CENTRE(ty);
    m->entities[i].angle = angle;
    m->entities[i].flags = BAST_ENT_SKILL_ALL;
    m->entities[i].tag = tag;
    return i;
}

static thinker_t *boss(game_state *gs)
{
    thinker_t *t = thinker_get(&gs->things, gs->boss.id);
    assert(t && t->type == BAST_ENT_WOOFU);
    return t;
}

static int count_live(const game_state *gs, uint16_t type)
{
    int n = 0;
    for (int i = 0; i < gs->things.count; i++)
        n += gs->things.list[i].type == type && thinker_alive(&gs->things.list[i]);
    return n;
}

static int heard(const game_state *gs, int sfx)
{
    for (int i = 0; i < gs->sounds.count; i++)
        if (gs->sounds.ev[i].id == sfx)
            return 1;
    return 0;
}

/* Boss arena 24x12, the player sealed in a 3x3 room at the west end (no
 * sight, no route): the boss and its Chompers never reach or see it. */
static bast_map *sealed_map(void)
{
    bast_map *m = open_map(24, 12);
    for (int y = 0; y < 12; y++)
        bast_map_cell_at(m, 4, y)->flags = BAST_CELL_SOLID;
    add_entity(m, BAST_ENT_PLAYER_START, 2, 5, 0, 0);
    add_entity(m, BAST_ENT_WOOFU, 16, 5, ANG180, 0);
    return m;
}

/* The fan: five boss bolts after the 24-tic wind-up, at the player's
 * bearing + (k - 2) * 2048, 10..20 each. */
static void test_fan(void)
{
    angle_t a[BOSS_FAN_BOLTS];
    boss_fan_angles(1000, a);
    printf("fan: angles around 1000:");
    for (int k = 0; k < BOSS_FAN_BOLTS; k++)
        printf(" %u", (unsigned)a[k]);
    printf("\n");
    assert(a[0] == (angle_t)(1000 - 4096) && a[1] == (angle_t)(1000 - 2048) && a[2] == 1000 &&
           a[3] == 3048 && a[4] == 5096);
    const projectile_info_t *pi = projectile_info(TH_TYPE_BOSS_BOLT);
    assert(pi && pi->speed == 16852 && pi->dmg_base == 10 && pi->dmg_spread == 10);

    bast_map *m = open_map(24, 12);
    add_entity(m, BAST_ENT_PLAYER_START, 3, 2, 0, 0);
    add_entity(m, BAST_ENT_WOOFU, 16, 7, ANG180, 0);
    game_state gs;
    game_init(&gs, m, 8, 2, 0);
    gs.player.health = 100000;
    int windup_tic = -1, fire_tic = -1;
    for (int tic = 1; tic <= 400 && fire_tic < 0; tic++) {
        game_tic(&gs, &IDLE);
        gs.player.health = 100000;
        if (windup_tic < 0 && boss(&gs)->state == TH_STATE_WINDUP)
            windup_tic = tic;
        int n = 0;
        angle_t got[8];
        for (int i = 0; i < gs.things.count; i++) {
            const thinker_t *t = &gs.things.list[i];
            if (t->type == TH_TYPE_BOSS_BOLT && t->state == TH_STATE_STAND && n < 8)
                got[n++] = t->angle;
        }
        if (n) {
            fire_tic = tic;
            const thinker_t *b = boss(&gs);
            const angle_t bearing = angle_of(gs.player.x - b->x, gs.player.y - b->y);
            boss_fan_angles(bearing, a);
            printf("fan: wind-up at tic %d, %d bolts at tic %d, bearing %u:", windup_tic, n,
                   tic, (unsigned)bearing);
            for (int k = 0; k < n; k++)
                printf(" %u", (unsigned)got[k]);
            printf("\n");
            assert(n == BOSS_FAN_BOLTS);
            for (int k = 0; k < n; k++)
                assert(got[k] == a[k]);
            assert(heard(&gs, SFX_fan_fire));
        }
    }
    assert(windup_tic > 0 && fire_tic == windup_tic + BOSS_FAN_WINDUP);
    bast_map_free(m);
}

/* Phase switch at HP 1200: a 35-tic flinch, then speed x1.5; no other pain. */
static void test_phase(void)
{
    bast_map *m = open_map(32, 8);
    add_entity(m, BAST_ENT_PLAYER_START, 2, 3, 0, 0);
    add_entity(m, BAST_ENT_WOOFU, 28, 3, ANG180, 0);
    game_state gs;
    game_init(&gs, m, 8, 2, 0);
    thinker_t *b = boss(&gs);
    b->state = TH_STATE_CHASE;
    assert(gs.boss.phase == 1);
    for (int i = 0; i < 1199; i++)
        ai_damage(&gs, b, 1); /* never flinches in phase 1: pain 0 */
    assert(b->health == 1201 && b->state == TH_STATE_CHASE && gs.boss.phase == 1);
    ai_damage_pain(&gs, b, 0, 35); /* no Howler stun either */
    assert(b->state == TH_STATE_CHASE);
    ai_damage(&gs, b, 1);
    printf("phase: HP %d phase %d state %d tics %d lid_blast %d\n", b->health, gs.boss.phase,
           b->state, b->tics, heard(&gs, SFX_lid_blast));
    assert(b->health == 1200 && gs.boss.phase == 2 && b->state == TH_STATE_PAIN &&
           b->tics == BOSS_FLINCH_TICS && heard(&gs, SFX_lid_blast));
    assert(gs.boss.pod_tics == BOSS_POD_TICS_2); /* 700 left, cut to 525 */
    ai_damage(&gs, b, 1); /* hits during the flinch do not restart it */
    assert(b->tics == BOSS_FLINCH_TICS);
    gs.player.health = 100000;
    fix_t x0 = b->x;
    int flinch = 0;
    while (b->state == TH_STATE_PAIN) {
        game_tic(&gs, &IDLE);
        flinch++;
        assert(b->x == x0);
    }
    fix_t max_step = 0;
    for (int i = 0; i < 16; i++) {
        const fix_t x = b->x;
        game_tic(&gs, &IDLE);
        gs.player.health = 100000;
        if (x - b->x > max_step)
            max_step = x - b->x;
    }
    printf("phase: flinch %d tics, then max step %d (phase 1 speed %d)\n", flinch, max_step,
           enemy_info(BAST_ENT_WOOFU)->speed);
    assert(flinch == BOSS_FLINCH_TICS);
    assert(max_step == enemy_info(BAST_ENT_WOOFU)->speed * 3 / 2);
    bast_map_free(m);
}

/* Pods open at 700 and 1400 awake tics in phase 1, alternating pairs, then
 * every 525 tics from the phase switch. Spawned Chompers count in kills
 * total and are awake. */
static void test_pods(void)
{
    bast_map *m = sealed_map();
    add_entity(m, BAST_ENT_BOSS_POD, 8, 1, ANG90, 0);
    add_entity(m, BAST_ENT_BOSS_POD, 8, 10, ANG270, 0);
    add_entity(m, BAST_ENT_BOSS_POD, 20, 1, ANG90, 0);
    add_entity(m, BAST_ENT_BOSS_POD, 20, 10, ANG270, 0);
    game_state gs;
    game_init(&gs, m, 8, 2, 0);
    assert(gs.boss.pod_count == 4);
    /* Asleep: the timer does not run. */
    for (int i = 0; i < 800; i++)
        game_tic(&gs, &IDLE);
    assert(gs.boss.pods_opened == 0 && gs.things.kill_total == 1);
    boss(&gs)->state = TH_STATE_CHASE;
    int opened_at[8], n = 0, prev = 0;
    for (int i = 1; i <= 1400; i++) {
        game_tic(&gs, &IDLE);
        if (gs.boss.pods_opened != prev) {
            opened_at[n++] = i;
            prev = gs.boss.pods_opened;
            assert(heard(&gs, SFX_pod_open));
        }
    }
    printf("pods: phase 1 opened at awake tics %d %d, %d Chompers, kills total %d\n",
           opened_at[0], opened_at[1], count_live(&gs, BAST_ENT_CHIHUAHUA_CHOMPER),
           gs.things.kill_total);
    assert(n == 2 && opened_at[0] == 700 && opened_at[1] == 1400 && gs.boss.pods_opened == 4);
    assert(count_live(&gs, BAST_ENT_CHIHUAHUA_CHOMPER) == 8 && gs.things.kill_total == 9);
    /* First pair from pods 0 and 1 (x = 8), the second from 2 and 3 (x = 20). */
    int west = 0, east = 0;
    for (int i = 0; i < gs.things.count; i++) {
        const thinker_t *t = &gs.things.list[i];
        if (t->type != BAST_ENT_CHIHUAHUA_CHOMPER)
            continue;
        assert(t->state != TH_STATE_STAND);
        if (i < 5) /* spawned first: boss is thinker 1 */
            west += fix_to_int(t->x) == 8;
        else
            east += fix_to_int(t->x) == 20;
    }
    assert(west == 4 && east == 4);
    /* Kill them all so the cap does not hide the phase-2 openings. */
    for (int i = 0; i < gs.things.count; i++)
        if (gs.things.list[i].type == BAST_ENT_CHIHUAHUA_CHOMPER)
            ai_damage(&gs, &gs.things.list[i], 1000);
    for (int i = 1; i <= 100; i++)
        game_tic(&gs, &IDLE);
    ai_damage(&gs, boss(&gs), 1200); /* to 1200: phase 2 */
    assert(gs.boss.phase == 2);
    n = 0;
    prev = gs.boss.pods_opened;
    for (int i = 1; i <= 1575; i++) {
        game_tic(&gs, &IDLE);
        if (gs.boss.pods_opened != prev) {
            opened_at[n++] = i;
            prev = gs.boss.pods_opened;
        }
        for (int k = 0; k < gs.things.count; k++) /* keep under the cap */
            if (gs.things.list[k].type == BAST_ENT_CHIHUAHUA_CHOMPER)
                ai_damage(&gs, &gs.things.list[k], 1000);
    }
    printf("pods: phase 2 opened at %d %d %d tics after the switch\n", opened_at[0],
           opened_at[1], opened_at[2]);
    assert(n == 3 && opened_at[0] == 525 && opened_at[1] == 1050 && opened_at[2] == 1575);
    bast_map_free(m);
}

/* Never more than 8 live Chompers, map-placed ones included. */
static void test_cap(void)
{
    bast_map *m = sealed_map();
    for (int i = 0; i < 7; i++)
        add_entity(m, BAST_ENT_CHIHUAHUA_CHOMPER, 6 + 2 * i, 9, 0, 0);
    add_entity(m, BAST_ENT_BOSS_POD, 8, 1, ANG90, 0);
    add_entity(m, BAST_ENT_BOSS_POD, 20, 1, ANG90, 0);
    game_state gs;
    game_init(&gs, m, 8, 2, 0);
    boss(&gs)->state = TH_STATE_CHASE;
    for (int i = 0; i < 700; i++)
        game_tic(&gs, &IDLE);
    const int after1 = count_live(&gs, BAST_ENT_CHIHUAHUA_CHOMPER);
    const int total1 = gs.things.kill_total;
    for (int i = 0; i < 700; i++)
        game_tic(&gs, &IDLE);
    printf("cap: 7 placed, after pods 1: %d live (kills total %d), after pods 2: %d live\n",
           after1, total1, count_live(&gs, BAST_ENT_CHIHUAHUA_CHOMPER));
    assert(after1 == BOSS_MAX_CHOMPERS && total1 == 1 + 8);
    assert(count_live(&gs, BAST_ENT_CHIHUAHUA_CHOMPER) == BOSS_MAX_CHOMPERS &&
           gs.things.kill_total == 9 && gs.boss.pods_opened == 4);
    /* Two die: the next opening refills to 8, no more. */
    int killed = 0;
    for (int i = 0; i < gs.things.count && killed < 2; i++)
        if (gs.things.list[i].type == BAST_ENT_CHIHUAHUA_CHOMPER)
            killed += ai_damage(&gs, &gs.things.list[i], 1000);
    for (int i = 0; i < 700; i++)
        game_tic(&gs, &IDLE);
    printf("cap: after 2 deaths and pods 3: %d live\n", count_live(&gs, BAST_ENT_CHIHUAHUA_CHOMPER));
    assert(count_live(&gs, BAST_ENT_CHIHUAHUA_CHOMPER) == BOSS_MAX_CHOMPERS);
    bast_map_free(m);
}

/* The boss's tag fires 105 tics after HP reaches 0 and opens the door. */
static void test_tag(void)
{
    bast_map *m = sealed_map();
    bast_map_cell_at(m, 22, 5)->flags = BAST_CELL_DOOR;
    bast_map_cell_at(m, 22, 5)->tag = 7;
    for (int y = 1; y < 11; y++)
        if (y != 5)
            bast_map_cell_at(m, 22, y)->flags = BAST_CELL_SOLID;
    m->entities[1].tag = 7; /* the boss */
    game_state gs;
    game_init(&gs, m, 8, 2, 0);
    assert(gs.boss.tag == 7);
    for (int i = 0; i < 10; i++)
        game_tic(&gs, &IDLE);
    /* Killed between tics: as if by this tic's shot (weapons fire before
     * the AI thinks), so the tag fires on the 105th tic from here. */
    const uint32_t kill_tic = gs.tic;
    assert(ai_damage(&gs, boss(&gs), 5000) == 1);
    assert(heard(&gs, SFX_woofu_death));
    int door = door_at(&gs, 22, 5);
    assert(door >= 0);
    int fired_at = -1;
    for (int i = 0; i < 200 && fired_at < 0; i++) {
        const uint32_t tic = gs.tic;
        game_tic(&gs, &IDLE);
        if (gs.last_tag == 7)
            fired_at = (int)(tic - kill_tic);
        else
            assert(gs.doors[door].state == DOOR_CLOSED);
    }
    printf("tag: fired on tic %d after the kill, door state %d\n", fired_at,
           gs.doors[door].state);
    assert(fired_at == BOSS_DEATH_TICS && gs.doors[door].state != DOOR_CLOSED);
    bast_map_free(m);
}

/* Phase 2: double fans 12 tics apart, then a barrage of 3 reticles on the
 * player and 1.5 u either side across the boss-player line; alternating. */
static void test_barrage(void)
{
    fix_t x[3], y[3];
    boss_barrage_points(CENTRE(16), CENTRE(5), CENTRE(4), CENTRE(5), x, y);
    printf("barrage: points (%d,%d) (%d,%d) (%d,%d)\n", x[0], y[0], x[1], y[1], x[2], y[2]);
    assert(x[0] == CENTRE(4) && y[0] == CENTRE(5));
    assert(x[1] == CENTRE(4) && y[1] == CENTRE(5) - BOSS_BARRAGE_SIDE); /* west + 90 = south */
    assert(x[2] == CENTRE(4) && y[2] == CENTRE(5) + BOSS_BARRAGE_SIDE);

    bast_map *m = open_map(24, 12);
    add_entity(m, BAST_ENT_PLAYER_START, 3, 5, 0, 0);
    add_entity(m, BAST_ENT_WOOFU, 16, 5, ANG180, 0);
    game_state gs;
    game_init(&gs, m, 8, 2, 0);
    gs.player.health = 100000;
    thinker_t *b = boss(&gs);
    b->state = TH_STATE_CHASE;
    ai_damage(&gs, b, 1200);
    assert(gs.boss.phase == 2);
    char seq[64];
    int ns = 0, fans[16], nf = 0;
    uint8_t was[MAX_THINKERS] = { 0 };
    for (int tic = 1; tic <= 1200 && ns < 9; tic++) {
        game_tic(&gs, &IDLE);
        gs.player.health = 100000;
        int bolts = 0, ret = 0;
        for (int i = 0; i < gs.things.count; i++) {
            const thinker_t *t = &gs.things.list[i];
            const uint8_t k = t->state != TH_STATE_STAND ? 0
                              : t->type == TH_TYPE_BOSS_BOLT ? 1
                              : t->type == TH_TYPE_RETICLE ? 2 : 0;
            if (k && k != was[i]) {
                bolts += k == 1;
                ret += k == 2;
            }
            was[i] = k;
        }
        if (bolts) {
            assert(bolts == BOSS_FAN_BOLTS && !ret);
            seq[ns++] = 'F';
            fans[nf++] = tic;
        }
        if (ret) {
            assert(ret == 3 && !bolts);
            seq[ns++] = 'R';
        }
    }
    seq[ns] = 0;
    printf("barrage: phase 2 attacks %s, first fans at %d and %d\n", seq, fans[0], fans[1]);
    assert(strncmp(seq, "FFRFFRFFR", 9) == 0);
    assert(fans[1] - fans[0] == BOSS_FAN_GAP);
    bast_map_free(m);
}

int main(int argc, char **argv)
{
    static const struct {
        const char *name;
        void (*fn)(void);
    } groups[] = {
        { "fan", test_fan }, { "phase", test_phase }, { "pods", test_pods },
        { "cap", test_cap }, { "tag", test_tag }, { "barrage", test_barrage },
    };
    for (size_t i = 0; i < sizeof groups / sizeof groups[0]; i++) {
        if (argc < 2 || strcmp(argv[1], groups[i].name) == 0) {
            groups[i].fn();
            if (argc >= 2)
                return 0;
        }
    }
    if (argc >= 2) {
        fprintf(stderr, "test_boss: unknown group %s\n", argv[1]);
        return 1;
    }
    return 0;
}
