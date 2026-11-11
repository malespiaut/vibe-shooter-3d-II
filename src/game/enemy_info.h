#ifndef GAME_ENEMY_INFO_H
#define GAME_ENEMY_INFO_H

/* Per-enemy stats (enemies.md Stats, AI (shared)), one table in
 * game/enemy_info.c: later enemies only add rows. Speeds are per tic,
 * 16.16; ranges are world units, 16.16. */

#include <stdint.h>

#include "engine/fixed.h"

enum {
    ATK_NONE = 0,    /* no attack yet (enemies whose attack lands later) */
    ATK_HITSCAN = 1, /* one ray at the player: hits when in sight and range */
    ATK_MELEE = 2,   /* hits when the player's near edge is within range */
};

typedef struct {
    uint16_t type;       /* enum bast_entity_type */
    int32_t hp;
    fix_t radius;        /* half the width */
    fix_t speed;         /* per tic */
    uint8_t attack;      /* ATK_* */
    int32_t dmg_base, dmg_spread; /* damage = base + rng() % (spread + 1) */
    fix_t range;         /* hitscan: max range; melee: reach */
    int32_t windup;      /* tics, skill 2 (Sabretooth: 25% shorter) */
    int32_t pain;        /* pain chance /256 per damaging hit (Sabretooth: half) */
    int32_t aggression;  /* ranged attack chance /256 per decision */
    uint16_t drop;       /* pickup entity type dropped on death, 0 = none */
    int32_t drop_amount; /* ammo in the drop, 0 = the pickup's normal amount */
} enemy_info_t;

/* The row for `type`, or NULL when `type` is not an enemy. */
const enemy_info_t *enemy_info(uint16_t type);

#endif
