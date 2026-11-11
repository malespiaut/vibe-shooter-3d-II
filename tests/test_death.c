/* Player death and level restart (hud.md Flow, items.md: the level restarts
 * with the inventory held at level entry). test_death <group>: timer,
 * early_use, input, restart, hash, eye. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "game/items.h"
#include "game/state.h"

#define FX(i) fix_from_int(i)
#define HALF (FIX_ONE / 2)

static bast_map *g_map;

/* 16x16 room, player at (3, 3), a Mutt Trooper far east and a Fish Snack. */
static bast_map *make_map(void)
{
    bast_map *m = bast_map_new(16, 16);
    assert(m);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            if (x == 0 || y == 0 || x == 15 || y == 15)
                bast_map_cell_at(m, x, y)->flags = BAST_CELL_SOLID;
    const int types[] = { BAST_ENT_PLAYER_START, BAST_ENT_MUTT_TROOPER, BAST_ENT_FISH_SNACK };
    const int pos[][2] = { { 3, 3 }, { 13, 13 }, { 6, 3 } };
    for (int k = 0; k < 3; k++) {
        int i = bast_map_add_entity(m);
        assert(i >= 0);
        bast_map_entity *e = &m->entities[i];
        e->type = (uint16_t)types[k];
        e->x = FX(pos[k][0]) + HALF;
        e->y = FX(pos[k][1]) + HALF;
        e->flags = BAST_ENT_SKILL1 | BAST_ENT_SKILL2 | BAST_ENT_SKILL3;
    }
    return m;
}

static const ticcmd NONE = { 0, 0, 0, 0 };
static const ticcmd USE = { 0, 0, 0, BT_USE };

static void kill(game_state *gs)
{
    gs->player.ninth_life = 0;
    gs->player.invuln_tics = 0;
    player_damage(gs, 10000);
    assert(gs->player.dead && gs->player.health == 0);
}

/* The death timer counts one per tic up to 70 and stops there. */
static void t_timer(void)
{
    game_state gs;
    game_init(&gs, g_map, 1, 2, 0);
    kill(&gs);
    assert(gs.player.death_tics == 0 && !game_restart_ready(&gs));
    for (int i = 1; i <= DEATH_VIEW_TICS; i++) {
        game_tic(&gs, &NONE);
        assert(gs.player.death_tics == i);
        assert(game_restart_ready(&gs) == (i == DEATH_VIEW_TICS));
    }
    for (int i = 0; i < 100; i++)
        game_tic(&gs, &NONE);
    assert(gs.player.death_tics == DEATH_VIEW_TICS && gs.player.dead);
}

/* Use during the 70 tics does nothing; after them it restarts. A use held
 * through tic 70 is not a new press: it must be released first. */
static void t_early_use(void)
{
    game_state gs;
    game_init(&gs, g_map, 1, 2, 0);
    kill(&gs);
    for (int i = 0; i < DEATH_VIEW_TICS; i++) {
        game_tic(&gs, (i & 1) ? &NONE : &USE); /* presses on every other tic */
        assert(gs.player.dead && gs.restarts == 0);
    }
    assert(game_restart_ready(&gs));
    game_tic(&gs, &USE); /* held: last tic was NONE (i = 69 odd), so an edge */
    assert(!gs.player.dead && gs.restarts == 1 && gs.tic == 0);

    /* Held from before tic 70: no edge, no restart until released. */
    game_init(&gs, g_map, 1, 2, 0);
    kill(&gs);
    for (int i = 0; i < DEATH_VIEW_TICS + 10; i++)
        game_tic(&gs, &USE);
    assert(gs.player.dead && gs.restarts == 0);
    game_tic(&gs, &NONE);
    game_tic(&gs, &USE);
    assert(!gs.player.dead && gs.restarts == 1);
}

/* Dead: movement, turning, fire and weapon select are ignored. */
static void t_input(void)
{
    game_state gs;
    game_init(&gs, g_map, 1, 2, 0);
    kill(&gs);
    const player_t *p = &gs.player;
    const fix_t x = p->x, y = p->y;
    const angle_t a = p->angle;
    const uint32_t shots = p->weapons.shots;
    const int32_t charges = p->weapons.ammo[AM_CHARGES];
    const ticcmd all = { 127, 127, 1000, BT_FIRE | BT_RUN | (1 << BT_WEAPON_SHIFT) };
    for (int i = 0; i < 40; i++)
        game_tic(&gs, &all);
    assert(p->x == x && p->y == y && p->angle == a);
    assert(p->weapons.shots == shots && p->weapons.ammo[AM_CHARGES] == charges);
    assert(p->weapons.ready == WP_PISTOL && p->weapons.pending == 0);
    assert(gs.tic == 40); /* the world keeps running */
}

/* After the restart, health, armour, ammo and weapons are the entry
 * snapshot, keys and powerups are gone, the world is back at level start. */
static void t_restart(void)
{
    game_state gs;
    game_init(&gs, g_map, 1, 2, 0);
    player_t *p = &gs.player;
    /* Carried in: as if from the previous level. */
    inventory_t in = gs.entry;
    in.health = 77;
    in.armour = 40;
    in.armour_class = ARMOUR_LINEN;
    in.satchel = 1;
    in.weapons_owned |= 1u << WP_SCATTERGUN;
    in.weapon_ready = WP_SCATTERGUN;
    in.ammo[AM_SHELLS] = 12;
    in.ammo[AM_CHARGES] = 33;
    game_init_with(&gs, g_map, 1, 2, 0, &in);
    assert(p->health == 77 && p->weapons.ready == WP_SCATTERGUN);

    /* Play the level: pick things up, gain keys and powerups, then die. */
    game_give_keys(&gs, KEY_GREEN | KEY_VIOLET);
    p->weapons.ammo[AM_SHELLS] = 3;
    p->weapons.ammo[AM_CHARGES] = 90;
    p->weapons.owned |= 1u << (WP_SCATTERGUN + 1);
    p->armour = 150;
    p->armour_class = ARMOUR_PLATE;
    p->catnip_tics = 500;
    p->satchel = 1;
    gs.items_got = 1;
    gs.pickups[0].taken = 1;
    for (int i = 0; i < 30; i++)
        game_tic(&gs, &NONE);
    kill(&gs);
    for (int i = 0; i < DEATH_VIEW_TICS; i++)
        game_tic(&gs, &NONE);
    game_tic(&gs, &USE);

    assert(!p->dead && p->death_tics == 0);
    assert(p->health == 77 && p->armour == 40 && p->armour_class == ARMOUR_LINEN);
    assert(p->satchel == 1);
    assert(p->weapons.owned == in.weapons_owned && p->weapons.ready == WP_SCATTERGUN);
    for (int i = 0; i < AM_TYPES; i++)
        assert(p->weapons.ammo[i] == in.ammo[i]);
    assert(p->keys == 0 && p->catnip_tics == 0 && p->catseye_tics == 0 && !p->ninth_life);
    assert(gs.tic == 0 && gs.items_got == 0 && !gs.pickups[0].taken);
    assert(gs.things.kills == 0);
    /* The snapshot survives the restart, for the next death. */
    assert(memcmp(&gs.entry, &in, sizeof in) == 0);
    assert(p->x == FX(3) + HALF && p->y == FX(3) + HALF);
}

/* Right after a restart the state hash equals the hash at the first level
 * start (same entry inventory), RNG included. */
static void t_hash(void)
{
    static game_state a, b;
    game_init(&a, g_map, 1, 2, 7);
    const uint64_t h0 = game_hash(&a);
    /* Burn RNG, move, fight a bit, then die and restart. */
    const ticcmd walk = { 127, 0, 300, BT_FIRE };
    for (int i = 0; i < 200; i++)
        game_tic(&a, &walk);
    assert(a.rng.state != 0);
    kill(&a);
    for (int i = 0; i < DEATH_VIEW_TICS; i++)
        game_tic(&a, &NONE);
    game_tic(&a, &USE);
    assert(game_hash(&a) == h0);

    /* Replaying from there matches a fresh start tic for tic. */
    game_init(&b, g_map, 1, 2, 7);
    for (int i = 0; i < 100; i++) {
        game_tic(&a, &walk);
        game_tic(&b, &walk);
        assert(game_hash(&a) == game_hash(&b));
    }

    /* A different entry inventory gives a different hash. */
    inventory_t in = b.entry;
    in.health = 50;
    game_init_with(&b, g_map, 1, 2, 7, &in);
    assert(game_hash(&b) != h0);
    /* And the death timer is hashed. */
    game_init(&a, g_map, 1, 2, 7);
    game_init(&b, g_map, 1, 2, 7);
    kill(&a);
    kill(&b);
    game_tic(&a, &NONE);
    assert(game_hash(&a) != game_hash(&b));
}

/* Eye height: 0.5 alive, dropping linearly to 0.125 over 70 tics. */
static void t_eye(void)
{
    game_state gs;
    game_init(&gs, g_map, 1, 2, 0);
    assert(game_eye_height(&gs) == FIX_ONE / 2);
    kill(&gs);
    assert(game_eye_height(&gs) == FIX_ONE / 2);
    fix_t last = game_eye_height(&gs);
    for (int i = 0; i < DEATH_VIEW_TICS; i++) {
        game_tic(&gs, &NONE);
        const fix_t e = game_eye_height(&gs);
        assert(e < last);
        last = e;
    }
    assert(last == FIX_ONE / 8);
    game_tic(&gs, &NONE);
    assert(game_eye_height(&gs) == FIX_ONE / 8);
}

int main(int argc, char **argv)
{
    static const struct {
        const char *name;
        void (*fn)(void);
    } groups[] = {
        { "timer", t_timer },     { "early_use", t_early_use }, { "input", t_input },
        { "restart", t_restart }, { "hash", t_hash },           { "eye", t_eye },
    };
    g_map = make_map();
    int ran = 0;
    for (size_t i = 0; i < sizeof groups / sizeof groups[0]; i++)
        if (argc < 2 || strcmp(argv[1], groups[i].name) == 0) {
            groups[i].fn();
            printf("death_%s: ok\n", groups[i].name);
            ran++;
        }
    bast_map_free(g_map);
    return ran ? 0 : 1;
}
