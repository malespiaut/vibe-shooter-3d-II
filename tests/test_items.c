/* Pickups, inventory and player damage (items.md tables, weapons.md Ammo
 * types, tech.md Skill). test_items <group>: health, armour, ammo, weapons,
 * keys, powerups, damage, ninth_life, spawn, touch. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "game/items.h"
#include "game/state.h"

#define FX(i) fix_from_int(i)
#define HALF (FIX_ONE / 2)

static bast_map *g_map;

static bast_map *open_map(void)
{
    bast_map *m = bast_map_new(16, 16);
    assert(m);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            if (x == 0 || y == 0 || x == 15 || y == 15)
                bast_map_cell_at(m, x, y)->flags = BAST_CELL_SOLID;
    return m;
}

static int add(bast_map *m, int type, int tx, int ty, uint16_t flags, uint8_t tag)
{
    int i = bast_map_add_entity(m);
    assert(i >= 0);
    bast_map_entity *e = &m->entities[i];
    e->type = (uint16_t)type;
    e->x = FX(tx) + HALF;
    e->y = FX(ty) + HALF;
    e->flags = flags;
    e->tag = tag;
    return i;
}

static void fresh(game_state *gs, int skill)
{
    game_init(gs, g_map, 1, skill, 0);
}

static void t_start(void)
{
    game_state gs;
    fresh(&gs, 2);
    const player_t *p = &gs.player;
    assert(p->health == 100 && p->armour == 0 && p->armour_class == ARMOUR_NONE);
    assert(p->weapons.owned == ((1u << WP_CLAWS) | (1u << WP_PISTOL)));
    assert(p->weapons.ready == WP_PISTOL && !p->satchel);
    assert(p->weapons.ammo[AM_CHARGES] == 50 && p->weapons.ammo[AM_SHELLS] == 0);
    assert(p->weapons.ammo[AM_SCARABS] == 0 && p->weapons.ammo[AM_CELLS] == 0);
    assert(p->keys == 0 && !p->ninth_life && !p->catnip_tics && !p->catseye_tics && !p->dead);
}

static void t_health(void)
{
    game_state gs;
    fresh(&gs, 2);
    player_t *p = &gs.player;
    /* Full health: Fish Snack and Tuna Tin stay. */
    assert(!item_apply(&gs, BAST_ENT_FISH_SNACK) && p->health == 100);
    assert(!item_apply(&gs, BAST_ENT_TUNA_TIN) && p->health == 100);
    p->health = 50;
    assert(item_apply(&gs, BAST_ENT_FISH_SNACK) && p->health == 60);
    assert(item_apply(&gs, BAST_ENT_TUNA_TIN) && p->health == 85);
    assert(item_apply(&gs, BAST_ENT_TUNA_TIN) && p->health == 100); /* cap 100 */
    p->health = 95;
    assert(item_apply(&gs, BAST_ENT_FISH_SNACK) && p->health == 100);
    /* Golden Sardine: +100, cap 200, works over 100. */
    assert(item_apply(&gs, BAST_ENT_GOLDEN_SARDINE) && p->health == 200);
    assert(!item_apply(&gs, BAST_ENT_GOLDEN_SARDINE) && p->health == 200);
    p->health = 150;
    assert(item_apply(&gs, BAST_ENT_GOLDEN_SARDINE) && p->health == 200);
    /* Above 100 (from a Sardine) a Fish Snack cannot be used. */
    assert(!item_apply(&gs, BAST_ENT_FISH_SNACK) && p->health == 200);
    p->health = 10;
    assert(item_apply(&gs, BAST_ENT_GOLDEN_SARDINE) && p->health == 110);
    puts("items_health: ok");
}

static void t_armour(void)
{
    game_state gs;
    fresh(&gs, 2);
    player_t *p = &gs.player;
    /* Linen Wrap: armour to 100 if below, class Linen. */
    assert(item_apply(&gs, BAST_ENT_LINEN_WRAP));
    assert(p->armour == 100 && p->armour_class == ARMOUR_LINEN);
    assert(!item_apply(&gs, BAST_ENT_LINEN_WRAP) && p->armour == 100);
    p->armour = 40;
    assert(item_apply(&gs, BAST_ENT_LINEN_WRAP) && p->armour == 100);
    /* Scarab Plate: armour to 200 if below, class Plate. */
    assert(item_apply(&gs, BAST_ENT_SCARAB_PLATE));
    assert(p->armour == 200 && p->armour_class == ARMOUR_PLATE);
    assert(!item_apply(&gs, BAST_ENT_SCARAB_PLATE));
    /* Plate at 150: a Linen Wrap is not below-100, so it stays, class kept. */
    p->armour = 150;
    assert(!item_apply(&gs, BAST_ENT_LINEN_WRAP) && p->armour_class == ARMOUR_PLATE);
    /* Plate at 50: Linen raises to 100 and sets class Linen. */
    p->armour = 50;
    assert(item_apply(&gs, BAST_ENT_LINEN_WRAP));
    assert(p->armour == 100 && p->armour_class == ARMOUR_LINEN);

    /* Bandage Scrap: +2, cap 200, keeps class; Linen when none. */
    fresh(&gs, 2);
    assert(item_apply(&gs, BAST_ENT_BANDAGE_SCRAP));
    assert(p->armour == 2 && p->armour_class == ARMOUR_LINEN);
    p->armour = 120;
    p->armour_class = ARMOUR_PLATE;
    assert(item_apply(&gs, BAST_ENT_BANDAGE_SCRAP));
    assert(p->armour == 122 && p->armour_class == ARMOUR_PLATE);
    p->armour = 199;
    assert(item_apply(&gs, BAST_ENT_BANDAGE_SCRAP) && p->armour == 200);
    assert(!item_apply(&gs, BAST_ENT_BANDAGE_SCRAP) && p->armour == 200);
    puts("items_armour: ok");
}

static const struct {
    int type, ammo, amount, max;
} AMMO_ROWS[] = {
    { BAST_ENT_CHARGE_VIAL, AM_CHARGES, 10, 200 }, { BAST_ENT_CHARGE_JAR, AM_CHARGES, 50, 200 },
    { BAST_ENT_YARN_BUNDLE, AM_SHELLS, 4, 50 },    { BAST_ENT_YARN_BASKET, AM_SHELLS, 20, 50 },
    { BAST_ENT_SCARAB, AM_SCARABS, 1, 50 },        { BAST_ENT_SCARAB_CRATE, AM_SCARABS, 5, 50 },
    { BAST_ENT_SUN_SHARD, AM_CELLS, 20, 300 },     { BAST_ENT_SUN_ORB, AM_CELLS, 100, 300 },
};
#define NROWS (int)(sizeof AMMO_ROWS / sizeof AMMO_ROWS[0])

static void t_ammo(void)
{
    game_state gs;
    player_t *p = &gs.player;
    for (int i = 0; i < NROWS; i++) {
        const int a = AMMO_ROWS[i].ammo;
        for (int skill = 1; skill <= 3; skill++) {
            fresh(&gs, skill);
            p->weapons.ammo[a] = 0;
            assert(item_apply(&gs, AMMO_ROWS[i].type));
            /* Kitten: 1.5x rounded down. */
            const int want = skill == 1 ? AMMO_ROWS[i].amount * 3 / 2 : AMMO_ROWS[i].amount;
            assert(p->weapons.ammo[a] == want);
            /* Cap at max; full ammo: the pickup stays. */
            assert(ammo_cap(a, p->satchel) == AMMO_ROWS[i].max);
            p->weapons.ammo[a] = AMMO_ROWS[i].max - 1;
            assert(item_apply(&gs, AMMO_ROWS[i].type) && p->weapons.ammo[a] == AMMO_ROWS[i].max);
            assert(!item_apply(&gs, AMMO_ROWS[i].type) && p->weapons.ammo[a] == AMMO_ROWS[i].max);
        }
    }
    /* Spot values of the Kitten rule: 15, 75, 6, 30, 1, 7, 30, 150. */
    fresh(&gs, 1);
    p->weapons.ammo[AM_SCARABS] = 0;
    assert(item_apply(&gs, BAST_ENT_SCARAB) && p->weapons.ammo[AM_SCARABS] == 1);
    assert(item_apply(&gs, BAST_ENT_SCARAB_CRATE) && p->weapons.ammo[AM_SCARABS] == 8);

    /* Litter Satchel: doubles max ammo, gives one small pickup of each. */
    fresh(&gs, 2);
    assert(item_apply(&gs, BAST_ENT_LITTER_SATCHEL));
    assert(p->satchel);
    assert(p->weapons.ammo[AM_CHARGES] == 60 && p->weapons.ammo[AM_SHELLS] == 4);
    assert(p->weapons.ammo[AM_SCARABS] == 1 && p->weapons.ammo[AM_CELLS] == 20);
    const int32_t max2[AM_TYPES] = { 400, 100, 100, 600 };
    for (int a = 0; a < AM_TYPES; a++) {
        assert(ammo_cap(a, p->satchel) == max2[a]);
        p->weapons.ammo[a] = max2[a] - 1;
    }
    assert(item_apply(&gs, BAST_ENT_CHARGE_JAR) && p->weapons.ammo[AM_CHARGES] == 400);
    assert(item_apply(&gs, BAST_ENT_SUN_ORB) && p->weapons.ammo[AM_CELLS] == 600);
    /* Second satchel with every type full: stays. */
    for (int a = 0; a < AM_TYPES; a++)
        p->weapons.ammo[a] = max2[a];
    assert(!item_apply(&gs, BAST_ENT_LITTER_SATCHEL));
    /* Second satchel with room: gives the small pickups, max stays x2. */
    p->weapons.ammo[AM_SHELLS] = 0;
    assert(item_apply(&gs, BAST_ENT_LITTER_SATCHEL) && p->weapons.ammo[AM_SHELLS] == 4);
    assert(ammo_cap(AM_SHELLS, p->satchel) == 100);
    /* Kitten satchel: small pickups at 1.5x. */
    fresh(&gs, 1);
    assert(item_apply(&gs, BAST_ENT_LITTER_SATCHEL));
    assert(p->weapons.ammo[AM_CHARGES] == 65 && p->weapons.ammo[AM_SHELLS] == 6);
    assert(p->weapons.ammo[AM_SCARABS] == 1 && p->weapons.ammo[AM_CELLS] == 30);
    puts("items_ammo: ok");
}

static void t_weapons(void)
{
    static const struct {
        int type, slot, ammo, amount;
    } W[] = {
        /* weapons.md Behaviour notes; the pistol gives a Charge Vial's 10
         * (weapon.c PICKUP_AMMO, the one weapon-pickup table). */
        { BAST_ENT_ANKH_PISTOL, 2, AM_CHARGES, 10 },
        { BAST_ENT_HAIRBALL_SCATTERGUN, 3, AM_SHELLS, 8 },
        { BAST_ENT_SISTRUM_REPEATER, 4, AM_CHARGES, 40 },
        { BAST_ENT_SCARAB_LAUNCHER, 5, AM_SCARABS, 4 },
        { BAST_ENT_EYE_OF_RA_LANCE, 6, AM_CELLS, 60 },
        { BAST_ENT_SPHINX_HOWLER, 7, AM_CELLS, 50 },
    };
    game_state gs;
    player_t *p = &gs.player;
    for (int i = 0; i < 6; i++) {
        fresh(&gs, 2);
        const int32_t before = p->weapons.ammo[W[i].ammo];
        assert(item_apply(&gs, W[i].type));
        assert(p->weapons.owned & (1u << W[i].slot));
        assert(p->weapons.ammo[W[i].ammo] == before + W[i].amount);
        /* Owned and ammo full: stays. */
        p->weapons.ammo[W[i].ammo] = ammo_cap(W[i].ammo, p->satchel);
        assert(!item_apply(&gs, W[i].type));
        /* Not owned but ammo full: taken for the weapon. */
        p->weapons.owned &= (uint8_t)~(1u << W[i].slot);
        assert(item_apply(&gs, W[i].type) && (p->weapons.owned & (1u << W[i].slot)));
        /* Kitten: weapon ammo is an ammo pickup too, 1.5x. */
        fresh(&gs, 1);
        const int32_t b1 = p->weapons.ammo[W[i].ammo];
        assert(item_apply(&gs, W[i].type));
        assert(p->weapons.ammo[W[i].ammo] == b1 + W[i].amount * 3 / 2);
        /* Satchel: the weapon's ammo fills up to twice ammo_max. */
        fresh(&gs, 2);
        p->satchel = 1;
        p->weapons.ammo[W[i].ammo] = ammo_max[W[i].ammo];
        assert(item_apply(&gs, W[i].type));
        assert(p->weapons.ammo[W[i].ammo] == ammo_max[W[i].ammo] + W[i].amount);
    }
    puts("items_weapons: ok");
}

static void t_keys(void)
{
    game_state gs;
    fresh(&gs, 2);
    player_t *p = &gs.player;
    assert(item_apply(&gs, BAST_ENT_GREEN_ANKH) && p->keys == KEY_GREEN);
    assert(!item_apply(&gs, BAST_ENT_GREEN_ANKH));
    assert(item_apply(&gs, BAST_ENT_BLUE_ANKH) && p->keys == (KEY_GREEN | KEY_BLUE));
    assert(item_apply(&gs, BAST_ENT_VIOLET_ANKH) && p->keys == (KEY_GREEN | KEY_BLUE | KEY_VIOLET));
    puts("items_keys: ok");
}

static void t_powerups(void)
{
    game_state gs;
    fresh(&gs, 2);
    player_t *p = &gs.player;
    const ticcmd idle = { 0, 0, 0, 0 };
    assert(item_apply(&gs, BAST_ENT_CATNIP_FRENZY) && p->catnip_tics == 1050);
    assert(item_apply(&gs, BAST_ENT_CATS_EYE_AMULET) && p->catseye_tics == 2100);
    for (int i = 0; i < 1049; i++)
        game_tic(&gs, &idle);
    assert(p->catnip_tics == 1 && p->catseye_tics == 1051);
    game_tic(&gs, &idle);
    assert(p->catnip_tics == 0);
    game_tic(&gs, &idle);
    assert(p->catnip_tics == 0 && p->catseye_tics == 1049);
    /* Taking another refreshes the timer. */
    assert(item_apply(&gs, BAST_ENT_CATS_EYE_AMULET) && p->catseye_tics == 2100);
    /* Ninth Life: carried, max 1. */
    assert(item_apply(&gs, BAST_ENT_NINTH_LIFE) && p->ninth_life == 1);
    assert(!item_apply(&gs, BAST_ENT_NINTH_LIFE));
    puts("items_powerups: ok");
}

static void t_damage(void)
{
    game_state gs;
    player_t *p = &gs.player;
    /* No armour. */
    fresh(&gs, 2);
    player_damage(&gs, 30);
    assert(p->health == 70 && p->armour == 0);
    /* Linen absorbs 1/3 rounded down, drains by that. */
    fresh(&gs, 2);
    item_apply(&gs, BAST_ENT_LINEN_WRAP);
    player_damage(&gs, 20); /* absorbs 6 */
    assert(p->health == 86 && p->armour == 94 && p->armour_class == ARMOUR_LINEN);
    player_damage(&gs, 2); /* absorbs 0 */
    assert(p->health == 84 && p->armour == 94);
    /* Plate absorbs 1/2 rounded down. */
    fresh(&gs, 2);
    item_apply(&gs, BAST_ENT_SCARAB_PLATE);
    player_damage(&gs, 25); /* absorbs 12 */
    assert(p->health == 87 && p->armour == 188 && p->armour_class == ARMOUR_PLATE);
    /* Absorption capped by armour left; class drops to none at 0. */
    p->armour = 3;
    player_damage(&gs, 40); /* would absorb 20, only 3 left */
    assert(p->health == 50 && p->armour == 0 && p->armour_class == ARMOUR_NONE);
    /* Kitten: half damage (rounded down, min 1), then armour. */
    fresh(&gs, 1);
    player_damage(&gs, 25);
    assert(p->health == 88);
    player_damage(&gs, 1);
    assert(p->health == 87);
    item_apply(&gs, BAST_ENT_SCARAB_PLATE);
    player_damage(&gs, 41); /* 20, plate absorbs 10 */
    assert(p->health == 77 && p->armour == 190);
    /* Sabretooth takes full damage. */
    fresh(&gs, 3);
    player_damage(&gs, 25);
    assert(p->health == 75);
    /* Death: health 0 and dead; further damage ignored, pickups ignored. */
    fresh(&gs, 2);
    player_damage(&gs, 150);
    assert(p->health == 0 && p->dead);
    player_damage(&gs, 10);
    assert(p->health == 0);
    puts("items_damage: ok");
}

static void t_ninth_life(void)
{
    game_state gs;
    player_t *p = &gs.player;
    const ticcmd idle = { 0, 0, 0, 0 };
    fresh(&gs, 2);
    item_apply(&gs, BAST_ENT_LINEN_WRAP);
    item_apply(&gs, BAST_ENT_NINTH_LIFE);
    player_damage(&gs, 300);
    assert(!p->dead && p->health == 50 && !p->ninth_life);
    assert(p->armour == 100 && p->armour_class == ARMOUR_LINEN); /* unchanged */
    assert(p->invuln_tics == 70);
    for (int i = 0; i < 69; i++) {
        game_tic(&gs, &idle);
        player_damage(&gs, 1000);
        assert(p->health == 50 && !p->dead);
    }
    game_tic(&gs, &idle);
    assert(p->invuln_tics == 0);
    player_damage(&gs, 10); /* linen absorbs 3 */
    assert(p->health == 43 && p->armour == 97);
    player_damage(&gs, 1000);
    assert(p->dead && p->health == 0);
    /* A non-lethal hit does not use it. */
    fresh(&gs, 2);
    item_apply(&gs, BAST_ENT_NINTH_LIFE);
    player_damage(&gs, 99);
    assert(p->health == 1 && p->ninth_life);
    puts("items_ninth_life: ok");
}

static void t_spawn(void)
{
    game_state gs;
    fresh(&gs, 1);
    assert(gs.pickup_count == 5); /* all-skill x3, skill1 x1, 1|2 x1 */
    assert(gs.items_total == 6);  /* + the Scattergun thinker */
    fresh(&gs, 2);
    assert(gs.pickup_count == 5); /* all-skill x3, skill2 x1, 1|2 x1 */
    assert(gs.items_total == 6);
    fresh(&gs, 3);
    assert(gs.pickup_count == 4); /* all-skill x3, skill3 x1 */
    assert(gs.items_total == 5);
    assert(gs.items_got == 0);
    /* The weapon pickup is a thinker only, never also a pickup_t. */
    for (int i = 0; i < gs.pickup_count; i++)
        assert(gs.pickups[i].type > BAST_ENT_SPHINX_HOWLER);
    puts("items_spawn: ok");
}

/* Walks the player east along row 1 over the pickups of the test map. */
static void t_touch(void)
{
    game_state gs;
    fresh(&gs, 2);
    player_t *p = &gs.player;
    const ticcmd walk = { 127, 0, 0, 0 };
    p->health = 90;
    /* Pickup at (3,1) is a Fish Snack, (5,1) Tuna Tin with PICKUP_TRIGGER
     * tag 7, (7,1) Charge Jar (skill 2). Player on row 1 facing east. */
    p->x = FX(1) + HALF;
    p->y = FX(1) + HALF;
    p->angle = 0;
    /* Just outside the 0.5 radius: not taken. */
    p->x = FX(3) + HALF - PICKUP_RADIUS - 1;
    game_tic(&gs, &(ticcmd){ 0, 0, 0, 0 });
    assert(gs.items_got == 0 && p->health == 90);
    p->x = FX(3) + HALF - PICKUP_RADIUS;
    game_tic(&gs, &(ticcmd){ 0, 0, 0, 0 });
    assert(gs.items_got == 1 && p->health == 100);
    for (int i = 0; i < 200 && p->x < FX(9); i++)
        game_tic(&gs, &walk);
    /* Tuna Tin at full health stays; the jar is taken. */
    assert(p->health == 100 && gs.items_got == 2 && p->weapons.ammo[AM_CHARGES] == 100);
    assert(gs.tags_fired == 0);
    /* Hurt, walk back: the Tuna Tin is taken now and fires tag 7. */
    p->health = 60;
    const uint64_t h = game_hash(&gs);
    p->angle = 0x8000;
    for (int i = 0; i < 200 && p->x > FX(4); i++)
        game_tic(&gs, &walk);
    assert(p->health == 85 && gs.items_got == 3);
    assert(gs.tags_fired == 1 && gs.last_tag == 7);
    assert(game_hash(&gs) != h);
    /* East again to the Scattergun thinker at (11,1): taken by items_touch
     * (weapon_give_ex), counted, fires tag 9; not in hand. */
    p->angle = 0;
    for (int i = 0; i < 300 && p->x < FX(13); i++)
        game_tic(&gs, &walk);
    assert(gs.items_got == 4 && gs.tags_fired == 2 && gs.last_tag == 9);
    assert(p->weapons.owned & (1u << WP_SCATTERGUN));
    assert(p->weapons.ammo[AM_SHELLS] == 8 && p->weapons.ready == WP_PISTOL);
    puts("items_touch: ok");
}

int main(int argc, char **argv)
{
    g_map = open_map();
    int ps = add(g_map, BAST_ENT_PLAYER_START, 1, 1, BAST_ENT_SKILL_ALL, 0);
    (void)ps;
    add(g_map, BAST_ENT_FISH_SNACK, 3, 1, BAST_ENT_SKILL_ALL, 0);
    add(g_map, BAST_ENT_TUNA_TIN, 5, 1, BAST_ENT_SKILL_ALL | BAST_ENT_PICKUP_TRIGGER, 7);
    add(g_map, BAST_ENT_CHARGE_JAR, 7, 1, BAST_ENT_SKILL2, 0);
    add(g_map, BAST_ENT_SUN_ORB, 7, 5, BAST_ENT_SKILL1, 0);
    add(g_map, BAST_ENT_SCARAB_PLATE, 9, 5, BAST_ENT_SKILL3, 0);
    add(g_map, BAST_ENT_NINTH_LIFE, 11, 5, BAST_ENT_SKILL1 | BAST_ENT_SKILL2, 0);
    add(g_map, BAST_ENT_HAIRBALL_SCATTERGUN, 11, 1,
        BAST_ENT_SKILL_ALL | BAST_ENT_PICKUP_TRIGGER, 9);
    add(g_map, BAST_ENT_LINEN_WRAP, 12, 12, BAST_ENT_SKILL_ALL, 0);
    add(g_map, BAST_ENT_WAYPOINT, 13, 13, BAST_ENT_SKILL_ALL, 0); /* not a pickup */
    add(g_map, BAST_ENT_MUTT_TROOPER, 13, 12, BAST_ENT_SKILL_ALL, 0); /* not a pickup */

    const char *g = argc > 1 ? argv[1] : "all";
    const int all = strcmp(g, "all") == 0;
    t_start();
    if (all || !strcmp(g, "health")) t_health();
    if (all || !strcmp(g, "armour")) t_armour();
    if (all || !strcmp(g, "ammo")) t_ammo();
    if (all || !strcmp(g, "weapons")) t_weapons();
    if (all || !strcmp(g, "keys")) t_keys();
    if (all || !strcmp(g, "powerups")) t_powerups();
    if (all || !strcmp(g, "damage")) t_damage();
    if (all || !strcmp(g, "ninth_life")) t_ninth_life();
    if (all || !strcmp(g, "spawn")) t_spawn();
    if (all || !strcmp(g, "touch")) t_touch();
    bast_map_free(g_map);
    return 0;
}
