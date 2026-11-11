#include "game/enemy_info.h"

#include <stddef.h>

#include "bast.h"

/* enemies.md Stats. Radius = width / 2. Enemies without an attack row yet
 * (ATK_NONE) wake and chase but never wind up; their issues fill it in. */
static const enemy_info_t ENEMIES[] = {
    /* type, hp, radius, speed, attack, dmg, range, windup, pain, aggression, drop */
    { BAST_ENT_MUTT_TROOPER, 20, 16384, 2809, ATK_HITSCAN, 3, 9, 20 * FIX_ONE, 20, 200, 64,
      BAST_ENT_CHARGE_VIAL, 5 },
    { BAST_ENT_CHIHUAHUA_CHOMPER, 15, 12288, 10299, ATK_MELEE, 4, 6, 39321 /* 0.6 */, 8, 160,
      0, 0, 0 },
    /* Bolt damage is the projectile row (game/projectile.c). */
    { BAST_ENT_POMERANIAN_PLASMAGUNNER, 45, 20480, 3745, ATK_PLASMA, 0, 0, 20 * FIX_ONE, 16, 128,
      48, 0, 0 },
    /* dmg/range: the headbutt, 5..10 under 1.5 u; the mortar is in ai.h. */
    { BAST_ENT_DACHSHUND_HOWITZER, 70, 32768, 2247, ATK_MORTAR, 5, 5, 98304 /* 1.5 */, 14, 100,
      40, 0, 0 },
    { BAST_ENT_MASTIFF_JUGGERNAUT, 400, 29491, 1872, ATK_NONE, 0, 0, 0, 24, 40, 32, 0, 0 },
    { BAST_ENT_SHEPHERD_SHIELDBEARER, 90, 20480, 4119, ATK_NONE, 0, 0, 0, 16, 64, 0, 0, 0 },
    { BAST_ENT_WOOFU, 2400, 49152, 1498, ATK_NONE, 0, 0, 0, 24, 0, 0, 0, 0 },
};

const enemy_info_t *enemy_info(uint16_t type)
{
    for (size_t i = 0; i < sizeof ENEMIES / sizeof ENEMIES[0]; i++)
        if (ENEMIES[i].type == type)
            return &ENEMIES[i];
    return NULL;
}
