#ifndef GAME_AI_H
#define GAME_AI_H

/* Shared enemy AI (enemies.md AI (shared), tech.md Skill). States on
 * thinker_t.state: STAND (idle, asleep) -> CHASE -> WINDUP -> ATTACK ->
 * CHASE; PAIN on a damaging hit; DEAD. Stats come from game/enemy_info.c.
 *
 * - Idle wakes on sight (player within +-60 deg of the facing, line of
 *   sight through live doors) or, unless AMBUSH, on hearing the player
 *   fire: the noise floods the tiles 8-connected to the player's that do
 *   not block (a closed door is a boundary).
 * - Chase walks tile to tile in the 8 compass directions, down a BFS
 *   distance field from the player's tile over the cells enemies may enter
 *   (not SOLID, BLOCK_ALL, PUSHWALL, BLOCK_MONSTER; doors that are open or
 *   unkeyed and not switch/timed/one-way), never cutting a corner. It picks
 *   the next tile on reaching a tile centre and at least every 8 tics, and
 *   opens the unkeyed door it walks into.
 * - Every 8 tics a ranged enemy with line of sight and the player in range
 *   winds up when rng() % 256 < aggression. A melee enemy winds up as soon
 *   as the player's near edge is within reach.
 * - Wind-up: stopped, facing the player, `windup` tics (Sabretooth: 25%
 *   shorter, rounded down); the attack resolves on the tic it ends, then
 *   AI_ATTACK_TICS of recovery. A hitscan shot hits when the player is in
 *   sight and range at that tic, a bite when still in reach.
 * - A damaging hit that does not kill wakes the enemy and, with chance
 *   pain / 256 (Sabretooth: half), flinches it for AI_PAIN_TICS, cancelling
 *   a wind-up. Death spawns the drop.
 * - Pomeranian (ATK_PLASMA) and Dachshund (ATK_MORTAR): see the AI_POM_
 *   and AI_DACH_ constants below. Enemy projectiles and shells hurt any
 *   shootable thinker but their owner; there is no infighting (nobody
 *   retargets).
 * - Missile enemies (Mastiff): the attack launches enemy_info.missile at
 *   the player from alternating shoulders (lateral offset enemy_info
 *   .shoulder, left first), aimed from the launch point; from the centre
 *   when the missile would overlap a wall at the shoulder.
 * - Collision: circle vs the tiles enemies may not stand in, then vs the
 *   player circle, axis-separated (x then y). */

#include "engine/render.h"
#include "game/state.h"

#define AI_DECIDE_TICS 8
#define AI_ATTACK_TICS 8
#define AI_PAIN_TICS 6
#define AI_FOV_HALF ((angle_t)10923) /* 60 degrees */
#define AI_SHIELD_HALF ((angle_t)10923) /* Shieldbearer shield: +-60 degrees */
#define AI_SHIELD_DROP_TICS 70          /* after a Sphinx Howler hit */
#define AI_MISSILE_Z ((fix_t)39322)     /* 0.6 u: missile launch height (render) */

/* How damage reaches a thinker, for the frontal shield. */
enum {
    DMG_DIRECT = 0, /* hitscan, the Lance, claws, a projectile's direct hit */
    DMG_SPLASH = 1, /* splash_damage */
};

/* Pomeranian (enemies.md): a volley of AI_POM_BOLTS bolts AI_POM_BOLT_GAP
 * tics apart, the first when the wind-up ends, each aimed at the player's
 * position on its fire tic; AI_ATTACK_TICS of recovery after the last. */
#define AI_POM_BOLTS 2
#define AI_POM_BOLT_GAP 10
#define AI_POM_BOLT_Z ((fix_t)24576) /* 0.375 u: the arm cannon, render-only */

/* Dachshund (enemies.md): keeps 6..10 u away, backs off under 4 u. */
#define AI_DACH_FLEE (4 * FIX_ONE)  /* starts backing off under this */
#define AI_DACH_NEAR (6 * FIX_ONE)  /* backs off until this far */
#define AI_DACH_FAR (10 * FIX_ONE)  /* holds within this, with sight */
#define AI_DACH_MORTAR_RANGE (20 * FIX_ONE) /* as the Mutt's rifle */

/* One tic of every live enemy, creation order. Called by game_tic after the
 * player moved and fired. */
void ai_tic(game_state *gs);

/* The player fired: wakes every idle, non-AMBUSH enemy in the area the
 * player stands in. */
void ai_noise(game_state *gs);

/* Damage from the player's weapons: thinker_damage, then wake/pain on a
 * survivable hit, or the drop on a kill. Returns 1 when it killed. */
int ai_damage(game_state *gs, thinker_t *t, int32_t damage);

/* ai_damage from a source at (sx, sy). Shieldbearer (enemy_info shield):
 * while its shield is up (ai_shield_up), damage whose source lies within
 * +-AI_SHIELD_HALF of its facing is blocked when DMG_DIRECT (0 damage,
 * shield_block plays, it wakes) and halved (rounded down) when DMG_SPLASH.
 * A source on its centre counts as in front. Everything else, and every
 * other thinker, is plain ai_damage. The Howler goes through
 * ai_damage_pain and is never blocked. */
int ai_damage_from(game_state *gs, thinker_t *t, int32_t damage, int kind, fix_t sx, fix_t sy);

/* 1 when `t` carries a frontal shield and it is up: not in WINDUP or
 * ATTACK (the shield swings aside) and no Howler drop running (a Howler
 * hit, thinker_t.howler_hit, drops it for AI_SHIELD_DROP_TICS tics: it is
 * down for the next AI_SHIELD_DROP_TICS game tics' weapon fire). */
int ai_shield_up(const thinker_t *t);

/* ai_damage with a forced flinch: when `pain_tics` > 0 a survivable hit
 * puts the enemy in PAIN for `pain_tics` tics without the pain roll (Sphinx
 * Howler, weapons.md); it resumes CHASE after. 0 = the normal pain roll. */
int ai_damage_pain(game_state *gs, thinker_t *t, int32_t damage, int32_t pain_tics);

/* Knockback: moves `t` by (dx, dy) in four quarter steps, each axis-
 * separated (x then y) and halved until it does not overlap a tile enemies
 * may not stand in. It stops at walls, never passes through one. */
void ai_push(game_state *gs, thinker_t *t, fix_t dx, fix_t dy);

/* 1 when enemy `t` has an unobstructed line to the player (live doors). */
int ai_line_of_sight(const game_state *gs, const thinker_t *t);

/* Wind-up length of `type` at the game's skill. */
int32_t ai_windup_tics(const game_state *gs, uint16_t type);

/* True when a circle of radius r at (x, y) overlaps a tile enemies may not
 * stand in (blocking tiles plus BLOCK_MONSTER). */
int ai_circle_blocked(const game_state *gs, fix_t x, fix_t y, fix_t r);

/* Fills `out` (up to `max`) with every enemy thinker, live or dead, in
 * creation order, with its sprite animation and frame (thinker_anim() at
 * gs->tic). Read only: the state hash is unchanged. Returns the count. */
int ai_render_things(const game_state *gs, render_thing *out, int max);

#endif
