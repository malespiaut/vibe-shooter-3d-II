#ifndef GAME_THINKER_H
#define GAME_THINKER_H

/* Thinkers (tech.md State hash): the generic actor list. A fixed-capacity
 * array in creation order; a thinker's id is its index + 1 and never
 * changes (thinkers are never removed, dead ones stay as corpses), so ids,
 * not pointers, are what other thinkers and the hash refer to. Id 0 means
 * "none". Enemies only stand still until the AI issue lands. */

#include <stdint.h>

#include "bast.h"
#include "engine/angle.h"
#include "engine/fixed.h"

#define MAX_THINKERS 256

enum {
    TH_STATE_STAND = 0, /* alive, idle */
    TH_STATE_DEAD = 1,  /* corpse: no longer hit, never removed */
    TH_STATE_TAKEN = 2, /* picked-up item: inert, never removed */
};

/* thinker_t.flags */
enum {
    TH_FLAG_COUNTKILL = 1 << 0, /* counts toward kills total */
    TH_FLAG_GIB = 1 << 1,       /* died at HP <= -(max HP / 2) */
    TH_FLAG_SHOOTABLE = 1 << 2, /* hitscan and melee can hit it (enemies) */
    TH_FLAG_PICKUP = 1 << 3,    /* touched by the player (weapon pickups) */
};

typedef struct {
    uint16_t type;  /* enum bast_entity_type */
    uint8_t state;  /* TH_STATE_* */
    uint8_t flags;  /* TH_FLAG_* */
    fix_t x, y;
    angle_t angle;
    int32_t tics;   /* tics left in the current state */
    int32_t health;
    int32_t max_health;
    fix_t radius;   /* half the width in enemies.md */
    uint16_t target; /* thinker id, 0 = none */
} thinker_t;

typedef struct {
    thinker_t list[MAX_THINKERS];
    int count;
    int32_t kills, kill_total;
} thinkers_t;

/* Enemy stats from enemies.md (HP, half width). Returns 0 when `type` is
 * not an enemy. */
int thinker_enemy_info(uint16_t type, int32_t *hp, fix_t *radius);

/* Empties the list and spawns every enemy and weapon-pickup entity of `map`
 * flagged for `skill` (1..3), in entity order. Other item types are added
 * by the pickups issue (#64). */
void thinkers_spawn_map(thinkers_t *th, const bast_map *map, int skill);

/* Appends an enemy; returns its id, or 0 when the list is full or `type`
 * is not an enemy. */
uint16_t thinker_spawn(thinkers_t *th, uint16_t type, fix_t x, fix_t y, angle_t angle);

/* Appends a weapon pickup (BAST_ENT_ANKH_PISTOL..SPHINX_HOWLER); returns
 * its id, or 0 when full or `type` is not a weapon. */
uint16_t thinker_spawn_pickup(thinkers_t *th, uint16_t type, fix_t x, fix_t y);

/* NULL for id 0 or an id that does not exist. */
thinker_t *thinker_get(thinkers_t *th, uint16_t id);

/* Applies `damage` to a live thinker: at HP <= 0 it becomes dead (counted
 * as a kill when it counts) and gibs at HP <= -(max HP / 2). Returns 1
 * when this hit killed it. */
int thinker_damage(thinkers_t *th, thinker_t *t, int32_t damage);

#endif
