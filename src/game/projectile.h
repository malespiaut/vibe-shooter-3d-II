#ifndef GAME_PROJECTILE_H
#define GAME_PROJECTILE_H

/* Projectiles, splash damage and exploding barrels (weapons.md Scarab
 * Launcher, items.md Decorations, tech.md Damage). Projectiles and their
 * explosion effects are thinkers (TH_TYPE_*, game/thinker.h): a projectile
 * moves `momx, momy` per tic, explodes on a blocking tile (closed doors
 * included), on a live shootable thinker or on the player (never on its
 * owner), deals its direct damage to what it hit, then splash around the
 * impact point, and turns into its explosion effect in the same slot. The
 * effect lives FX_TICS_PER_FRAME tics per frame of its fx sprite, then the
 * slot is TH_STATE_FREE. The Pomeranian, Mastiff, Dachshund and boss
 * attacks add rows to the projectile table. Fixed point only. */

#include <stdint.h>

#include "engine/angle.h"
#include "engine/fixed.h"

struct game_state;

#define SPLASH_RADIUS_1_5 ((fix_t)98304) /* 1.5 u: scarab and barrel */
#define BARREL_SPLASH 64
#define FX_TICS_PER_FRAME 3
#define PROJ_SUBSTEPS 4 /* collision checks per tic of motion */

typedef struct {
    fix_t speed;            /* per tic, 16.16 */
    fix_t radius;
    int32_t dmg_base, dmg_spread; /* direct hit: base + rng % (spread + 1) */
    int32_t splash;         /* at the impact point, linear falloff to 0 */
    fix_t splash_radius;
    uint16_t fx;            /* effect thinker type it becomes */
} projectile_info_t;

/* NULL when `type` is not a projectile. */
const projectile_info_t *projectile_info(uint16_t type);

/* Explosion effect length in tics for effect type `fx`, 0 if not one. */
int32_t fx_tics(uint16_t fx);

/* Spawns projectile `type` at (x, y), height `z`, flying along `angle`,
 * fired by `owner` (thinker id or TH_ID_PLAYER). It first moves on the
 * next projectiles_tic. Returns its id, 0 when the list is full. */
uint16_t projectile_spawn(struct game_state *gs, uint16_t type, uint16_t owner, fix_t x, fix_t y,
                          fix_t z, angle_t angle);

/* Splash: every live shootable thinker except `source_id`, then the player,
 * whose centre is at distance d < radius from (x, y) with a clear line
 * (no blocking tile between the two centres) takes
 * amount * (radius - d) / radius, rounded down (nothing at 0). Every
 * thinker_alive shootable thinker is hit, awake enemies (CHASE, WINDUP,
 * ATTACK, PAIN) included. Thinkers go through ai_damage (wakes, pain
 * chance, drops; thinker_damage's gib rule; barrels light their fuse), the player
 * through player_damage. */
void splash_damage(struct game_state *gs, fix_t x, fix_t y, int32_t amount, fix_t radius,
                   uint16_t source_id);

/* One tic of the transient thinkers, in three passes: barrel fuses count
 * down (a barrel at 0 becomes TH_STATE_DEAD and is queued); projectiles
 * move and explode, effects count down; queued barrels explode in thinker
 * order (explosion_big effect, BARREL_SPLASH / 1.5 u). A barrel killed
 * during a tic is first counted down on the next one, so a chain explodes
 * exactly BARREL_FUSE_TICS apart. game_tic order: player move, items,
 * tags, projectiles_tic, weapon_tic, ai_noise, ai_tic. A scarab the player
 * fires (or an enemy projectile spawned in ai_tic) first moves on the next
 * tic, and enemies think after this tic's blasts. */
void projectiles_tic(struct game_state *gs);

#endif
