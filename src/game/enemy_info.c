#include "game/enemy_info.h"

#include <stddef.h>

#include "bast.h"
#include "game/boss.h"
#include "game/thinker.h"

/* enemies.md Stats. Radius = width / 2. Enemies without an attack row yet
 * (ATK_NONE) wake and chase but never wind up; their issues fill it in. */
static const enemy_info_t ENEMIES[] = {
    /* type, hp, radius, speed, attack, dmg, range, windup, pain, aggression, drop,
     * missile, shoulder, shield,
     * sight, windup, attack, pain, death (audio.md; Woofu has no wind-up voice,
     * the Dachshund's telegraph is reticle_beep) */
    { BAST_ENT_MUTT_TROOPER, 20, 16384, 2809, ATK_HITSCAN, 3, 9, 20 * FIX_ONE, 20, 200, 64,
      BAST_ENT_CHARGE_VIAL, 5, 0, 0, 0,
      ENEMY_SND(mutt_sight), ENEMY_SND(mutt_windup), ENEMY_SND(mutt_fire),
      ENEMY_SND(mutt_pain), ENEMY_SND(mutt_death) },
    { BAST_ENT_CHIHUAHUA_CHOMPER, 15, 12288, 10299, ATK_MELEE, 4, 6, 39321 /* 0.6 */, 8, 160,
      0, 0, 0, 0, 0, 0,
      ENEMY_SND(chomp_sight), ENEMY_SND(chomp_windup), ENEMY_SND(chomp_bite),
      ENEMY_SND(chomp_pain), ENEMY_SND(chomp_death) },
    /* Bolt damage is the projectile row (game/projectile.c). */
    { BAST_ENT_POMERANIAN_PLASMAGUNNER, 45, 20480, 3745, ATK_PLASMA, 0, 0, 20 * FIX_ONE, 16, 128,
      48, 0, 0, 0, 0, 0,
      ENEMY_SND(pom_sight), ENEMY_SND(pom_windup), ENEMY_SND(plasma_fire),
      ENEMY_SND(pom_pain), ENEMY_SND(pom_death) },
    /* dmg/range: the headbutt, 5..10 under 1.5 u; the mortar is in ai.h. */
    { BAST_ENT_DACHSHUND_HOWITZER, 70, 32768, 2247, ATK_MORTAR, 5, 5, 98304 /* 1.5 */, 14, 100,
      40, 0, 0, 0, 0, 0,
      ENEMY_SND(dach_sight), ENEMY_SND(reticle_beep), ENEMY_SND(mortar_launch),
      ENEMY_SND(dach_pain), ENEMY_SND(dach_death) },
    /* Bone missile: 20..40 direct + 32 splash r1.0 (projectile.c), from
     * alternating shoulders 0.35 u (22938) off the centre line. Range: the
     * Mutt's 20 u, the same "line of sight and a range check". */
    { BAST_ENT_MASTIFF_JUGGERNAUT, 400, 29491, 1872, ATK_MISSILE, 20, 20, 20 * FIX_ONE, 24, 40,
      32, 0, 0, TH_TYPE_BONE_PROJ, 22938, 0,
      ENEMY_SND(mastiff_sight), ENEMY_SND(mastiff_windup), ENEMY_SND(bone_fire),
      ENEMY_SND(mastiff_pain), ENEMY_SND(mastiff_death) },
    /* Shock baton 12..20 at 1.0 u; frontal shield. */
    { BAST_ENT_SHEPHERD_SHIELDBEARER, 90, 20480, 4119, ATK_MELEE, 12, 8, FIX_ONE, 16, 64, 0, 0,
      0, 0, 0, 1,
      ENEMY_SND(shep_sight), ENEMY_SND(shep_windup), ENEMY_SND(baton_hit),
      ENEMY_SND(shep_pain), ENEMY_SND(shep_death) },
    /* Boss: attacks and timings in game/boss.h; pain 0 (scripted flinch only). */
    { BAST_ENT_WOOFU, 2400, 49152, 1498, ATK_BOSS, 0, 0, BOSS_RANGE, BOSS_FAN_WINDUP, 0, 64, 0,
      0, 0, 0, 0,
      ENEMY_SND(woofu_sight), 0, ENEMY_SND(fan_fire),
      ENEMY_SND(woofu_pain), ENEMY_SND(woofu_death) },
};

const enemy_info_t *enemy_info(uint16_t type)
{
    for (size_t i = 0; i < sizeof ENEMIES / sizeof ENEMIES[0]; i++)
        if (ENEMIES[i].type == type)
            return &ENEMIES[i];
    return NULL;
}
