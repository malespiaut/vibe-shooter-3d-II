#ifndef GAME_BOSS_H
#define GAME_BOSS_H

/* Woofu, the Chrome Pharaoh (enemies.md Boss). The boss behaviour lives
 * in game/ai.c; this is its per-level state, kept in game_state.boss and
 * hashed. One boss per level: the first WOOFU thinker.
 *
 * - Phase 1 (HP 2400..1201): chases like any enemy; every AI_DECIDE_TICS,
 *   with sight and the player within BOSS_RANGE, it winds up
 *   (BOSS_FAN_WINDUP) when rng() % 256 < aggression, then fires a fan of
 *   BOSS_FAN_BOLTS boss bolts at the player's bearing + (k - 2) *
 *   BOSS_FAN_SPREAD, k = 0..4.
 * - Pods: the first BOSS_POD_TICS_1 tics the boss is awake (any state but
 *   STAND), then every BOSS_POD_TICS_1 in phase 1 and BOSS_POD_TICS_2 in
 *   phase 2 (at the phase switch a wait longer than BOSS_POD_TICS_2 is cut
 *   to it), it opens the
 *   next pair of BOSS_POD entities (pods 0 and 1, then 2 and 3, ...,
 *   wrapping, entity order). Each releases BOSS_POD_CHOMPERS awake
 *   Chompers 0.25 u either side of the pod along its facing's normal (at
 *   the pod centre when that spot is blocked), never past BOSS_MAX_CHOMPERS
 *   live Chompers. Spawned Chompers add to the kills total as they spawn.
 * - Phase switch: the hit that takes HP to <= BOSS_PHASE2_HP (and > 0)
 *   cancels any wind-up and forces a BOSS_FLINCH_TICS flinch (lid_blast);
 *   the boss never takes any other pain (pain 0, no Howler stun). Phase 2
 *   speed is 3/2 of phase 1. Its attacks alternate: two fans (the second
 *   BOSS_FAN_GAP tics after the first, re-aimed), then a mortar barrage
 *   (BOSS_BARRAGE_WINDUP, three reticles: on the player, and BOSS_BARRAGE_SIDE
 *   either side of the player perpendicular to the boss-player line; they
 *   land MORTAR_DELAY_TICS later), then fans again.
 * - Death: BOSS_DEATH_TICS after the tic HP reaches 0, the boss fires its
 *   entity tag (game_fire_tag). */

#include <stdint.h>

#include "engine/angle.h"
#include "engine/fixed.h"

#define BOSS_PHASE2_HP 1200
#define BOSS_FLINCH_TICS 35
#define BOSS_DEATH_TICS 105
#define BOSS_FAN_BOLTS 5
#define BOSS_FAN_SPREAD ((angle_t)2048)
#define BOSS_FAN_WINDUP 24
#define BOSS_FAN_GAP 12           /* phase 2: second fan after the first */
#define BOSS_BARRAGE_WINDUP 30    /* enemies.md wind-up 20-30 */
#define BOSS_BARRAGE_SIDE ((fix_t)98304) /* 1.5 u */
#define BOSS_RANGE (24 * FIX_ONE)
#define BOSS_BOLT_Z ((fix_t)49152) /* 0.75 u: the sceptre, render-only */
#define BOSS_POD_TICS_1 700
#define BOSS_POD_TICS_2 525
#define BOSS_POD_CHOMPERS 2
#define BOSS_MAX_CHOMPERS 8
#define BOSS_POD_SPREAD ((fix_t)16384) /* 0.25 u */
#define BOSS_MAX_PODS 16

enum { BOSS_NEXT_FANS = 0, BOSS_NEXT_BARRAGE = 1 };

typedef struct {
    fix_t x, y;
    angle_t angle; /* faces into the arena */
} boss_pod_t;

typedef struct {
    uint16_t id;        /* boss thinker id, 0 = no boss on this level */
    uint8_t phase;      /* 1 or 2 */
    uint8_t next;       /* phase 2: BOSS_NEXT_* attack after this wind-up */
    uint8_t attack;     /* the attack being wound up / resolved (BOSS_NEXT_*) */
    uint8_t tag;        /* fired on death (the boss entity's tag) */
    uint8_t tag_fired;
    int32_t pod_tics;   /* awake tics to the next pod opening */
    int32_t pod_pair;   /* next pair index */
    int32_t pods_opened; /* pods opened so far (tests) */
    int32_t death_tic;  /* gs->tic at which the tag fires; 0 = alive */
    int pod_count;
    boss_pod_t pods[BOSS_MAX_PODS]; /* static map data, not hashed */
} boss_t;

#endif
