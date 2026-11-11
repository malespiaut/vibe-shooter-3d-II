#ifndef GAME_DEMO_COMBAT_H
#define GAME_DEMO_COMBAT_H

/* Demo combat (tech.md Demos: face nearest, fire nearest, clear). Like
 * demo_nav, pure functions of the game state that produce one ordinary
 * ticcmd per tic; they never write game state. Integer / fixed point only. */

#include <stdint.h>

#include "game/state.h"

/* Hitscan fires when the aim error left after this tic's turn is under one
 * degree (65536 / 360 = 182 angle units). */
#define COMBAT_AIM_TOL 182
#define COMBAT_FIRE_DEFAULT_MAX 350
#define COMBAT_CLEAR_DEFAULT_MAX 2100
#define COMBAT_CLEAR_DEFAULT_RADIUS 10 /* world units */
/* clear strafes left for the first half of each period, right for the rest. */
#define COMBAT_STRAFE_PERIOD 35
#define COMBAT_STRAFE_LEFT_TICS 17
#define COMBAT_STRAFE_SPEED TICCMD_MAX

/* 1 when thinker `id` is a live enemy with an unobstructed line to the
 * player (ai_line_of_sight, live doors) and, when radius > 0, its centre
 * within `radius` of the player. */
int combat_visible(const game_state *gs, uint16_t id, fix_t radius);

/* The nearest visible live enemy (combat_visible with `radius`, 0 = any
 * distance): smallest distance, ties broken by the lower thinker id.
 * 0 when there is none. */
uint16_t combat_nearest(const game_state *gs, fix_t radius);

/* One tic of aiming at thinker `id`: turn toward its centre at most the
 * keyboard turn rate (PLAYER_TURN_SPEED). Returns the absolute aim error
 * left after this tic's turn. *out is cleared first, then only `turn` set. */
int32_t combat_aim(const game_state *gs, uint16_t id, ticcmd *out);

/* clear's strafe for its tic number `t` (0-based): -STRAFE_SPEED (left)
 * for the first COMBAT_STRAFE_LEFT_TICS tics of each period, then right. */
int8_t combat_strafe(int32_t t);

#endif
