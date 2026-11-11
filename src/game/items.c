#include "game/items.h"
#include "game/tags.h"

#include <string.h>

/* weapons.md Ammo types: small and large pickup, per AM_* type. Caps are
 * ammo_max[] (game/weapon.c); weapon pickups are weapon_give_ex. */
static const int32_t AMMO_SMALL[AM_TYPES] = { 10, 4, 1, 20 };
static const int32_t AMMO_LARGE[AM_TYPES] = { 50, 20, 5, 100 };

int item_is_pickup(int type)
{
    return type >= BAST_ENT_ANKH_PISTOL && type <= BAST_ENT_NINTH_LIFE;
}

void player_start_inventory(player_t *p)
{
    p->health = PLAYER_START_HEALTH;
    p->armour = 0;
    p->armour_class = ARMOUR_NONE;
    p->satchel = 0;
    p->catnip_tics = 0;
    p->catseye_tics = 0;
    p->ninth_life = 0;
    p->invuln_tics = 0;
    p->dead = 0;
}

void items_spawn(game_state *gs)
{
    const bast_map *m = gs->map;
    gs->pickup_count = 0;
    gs->items_got = 0;
    for (int i = 0; i < m->entity_count && gs->pickup_count < MAX_PICKUPS; i++) {
        const bast_map_entity *e = &m->entities[i];
        if (!item_is_pickup(e->type) || e->type <= BAST_ENT_SPHINX_HOWLER ||
            !entity_on_skill(e, gs->skill))
            continue;
        pickup_t *pk = &gs->pickups[gs->pickup_count++];
        pk->type = e->type;
        pk->flags = e->flags;
        pk->tag = e->tag;
        pk->taken = 0;
        pk->x = e->x;
        pk->y = e->y;
    }
    gs->items_total = gs->pickup_count;
    for (int i = 0; i < gs->things.count; i++)
        if (gs->things.list[i].flags & TH_FLAG_PICKUP)
            gs->items_total++;
}

/* Ammo pickup: weapon_add_ammo applies Kitten 1.5x and the Satchel cap. */
static int give_ammo(game_state *gs, int a, int32_t n)
{
    player_t *p = &gs->player;
    return weapon_add_ammo(&p->weapons, a, n, gs->skill == 1, p->satchel);
}

static int give_health(player_t *p, int32_t n, int32_t cap)
{
    if (p->health >= cap)
        return 0;
    p->health += n;
    if (p->health > cap)
        p->health = cap;
    return 1;
}

/* Linen Wrap / Scarab Plate: armour to `to` if below, with that class. */
static int give_armour(player_t *p, int32_t to, int32_t cls)
{
    if (p->armour >= to)
        return 0;
    p->armour = to;
    p->armour_class = cls;
    return 1;
}

static int give_key(player_t *p, uint32_t key)
{
    if (p->keys & key)
        return 0;
    p->keys |= key;
    return 1;
}

int item_apply(game_state *gs, int type)
{
    player_t *p = &gs->player;
    if (type >= BAST_ENT_CHARGE_VIAL && type <= BAST_ENT_SUN_ORB) {
        const int k = type - BAST_ENT_CHARGE_VIAL; /* vial, jar, bundle, ... */
        const int a = k / 2;
        return give_ammo(gs, a, (k & 1) ? AMMO_LARGE[a] : AMMO_SMALL[a]);
    }
    if (type >= BAST_ENT_ANKH_PISTOL && type <= BAST_ENT_SPHINX_HOWLER)
        return weapon_give_ex(&p->weapons, type - BAST_ENT_ANKH_PISTOL + WP_PISTOL,
                              gs->skill == 1, p->satchel);
    switch (type) {
    case BAST_ENT_FISH_SNACK:
        return give_health(p, 10, HEALTH_CAP);
    case BAST_ENT_TUNA_TIN:
        return give_health(p, 25, HEALTH_CAP);
    case BAST_ENT_GOLDEN_SARDINE:
        return give_health(p, 100, HEALTH_SUPER_CAP);
    case BAST_ENT_LINEN_WRAP:
        return give_armour(p, 100, ARMOUR_LINEN);
    case BAST_ENT_SCARAB_PLATE:
        return give_armour(p, 200, ARMOUR_PLATE);
    case BAST_ENT_BANDAGE_SCRAP:
        if (p->armour >= ARMOUR_CAP)
            return 0;
        p->armour += 2;
        if (p->armour > ARMOUR_CAP)
            p->armour = ARMOUR_CAP;
        if (p->armour_class == ARMOUR_NONE)
            p->armour_class = ARMOUR_LINEN;
        return 1;
    case BAST_ENT_LITTER_SATCHEL: {
        const int first = !p->satchel;
        p->satchel = 1;
        int used = first;
        for (int a = 0; a < AM_TYPES; a++)
            used |= give_ammo(gs, a, AMMO_SMALL[a]);
        return used;
    }
    case BAST_ENT_GREEN_ANKH:
        return give_key(p, KEY_GREEN);
    case BAST_ENT_BLUE_ANKH:
        return give_key(p, KEY_BLUE);
    case BAST_ENT_VIOLET_ANKH:
        return give_key(p, KEY_VIOLET);
    case BAST_ENT_CATNIP_FRENZY:
        p->catnip_tics = CATNIP_TICS;
        return 1;
    case BAST_ENT_CATS_EYE_AMULET:
        p->catseye_tics = CATSEYE_TICS;
        return 1;
    case BAST_ENT_NINTH_LIFE:
        if (p->ninth_life)
            return 0;
        p->ninth_life = 1;
        return 1;
    default:
        return 0;
    }
}

void items_touch(game_state *gs)
{
    const player_t *p = &gs->player;
    if (p->dead)
        return;
    const int64_t r2 = (int64_t)PICKUP_RADIUS * PICKUP_RADIUS;
    for (int i = 0; i < gs->pickup_count; i++) {
        pickup_t *pk = &gs->pickups[i];
        if (pk->taken)
            continue;
        const int64_t dx = (int64_t)pk->x - p->x, dy = (int64_t)pk->y - p->y;
        if (dx * dx + dy * dy > r2)
            continue;
        if (!item_apply(gs, pk->type))
            continue;
        pk->taken = 1;
        gs->items_got++;
        if (pk->flags & BAST_ENT_PICKUP_TRIGGER)
            game_fire_tag(gs, pk->tag);
    }
    for (int i = 0; i < gs->things.count; i++) {
        thinker_t *t = &gs->things.list[i];
        if (!(t->flags & TH_FLAG_PICKUP) || t->state != TH_STATE_STAND)
            continue;
        const int64_t dx = (int64_t)t->x - p->x, dy = (int64_t)t->y - p->y;
        if (dx * dx + dy * dy > r2)
            continue;
        if (!item_apply(gs, t->type))
            continue;
        t->state = TH_STATE_TAKEN;
        gs->items_got++;
        if (t->flags & TH_FLAG_TRIGGER)
            game_fire_tag(gs, t->tag);
    }
}

void items_tic(game_state *gs)
{
    player_t *p = &gs->player;
    if (p->catnip_tics > 0)
        p->catnip_tics--;
    if (p->catseye_tics > 0)
        p->catseye_tics--;
    if (p->invuln_tics > 0)
        p->invuln_tics--;
}

void player_damage(game_state *gs, int32_t amount)
{
    player_t *p = &gs->player;
    if (amount <= 0 || p->dead || p->invuln_tics > 0)
        return;
    if (gs->skill == 1) {
        amount /= 2;
        if (amount < 1)
            amount = 1;
    }
    const int32_t armour_before = p->armour, class_before = p->armour_class;
    int32_t absorbed = 0;
    if (p->armour_class == ARMOUR_LINEN)
        absorbed = amount / 3;
    else if (p->armour_class == ARMOUR_PLATE)
        absorbed = amount / 2;
    if (absorbed > p->armour)
        absorbed = p->armour;
    p->armour -= absorbed;
    if (p->armour == 0)
        p->armour_class = ARMOUR_NONE;
    p->health -= amount - absorbed;
    if (p->health > 0)
        return;
    if (p->ninth_life) {
        p->ninth_life = 0;
        p->health = NINTH_LIFE_HEALTH;
        p->armour = armour_before;
        p->armour_class = class_before;
        p->invuln_tics = NINTH_LIFE_INVULN_TICS;
        return;
    }
    p->health = 0;
    p->dead = 1;
}
