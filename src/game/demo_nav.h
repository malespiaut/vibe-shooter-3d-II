#ifndef GAME_DEMO_NAV_H
#define GAME_DEMO_NAV_H

/* Demo navigation (tech.md Demos: goto, face). Pure functions of the game
 * state that turn a goal into one ordinary ticcmd per tic; they never write
 * game state. Integer / fixed-point only. */

#include <stdint.h>

#include "game/state.h"

/* Arrival tolerance for goto: within 0.125 units of the tile centre. */
#define NAV_ARRIVE_DIST (FIX_ONE / 8)
/* face is done when within one fine-table step of the target angle. */
#define NAV_FACE_TOL (1 << ANGLETOFINESHIFT)

enum {
    NAV_MOVING = 0,  /* *out holds this tic's input */
    NAV_ARRIVED = 1, /* at the target centre and at rest: no tic used */
    NAV_NO_PATH = 2, /* target blocked or unreachable from the player */
};

/* Forward tics without the player changing tile after which goto treats
 * the tile it is steering into as blocked (issue #161). */
#define NAV_STALL_TICS 35

/* goto's memory between tics of one goto command, derived only from game
 * state (deterministic). Reset with nav_reset when a goto starts. */
typedef struct {
    int32_t tile;  /* player tile index (y * width + x) last tic, -1 none */
    int32_t stall; /* forward tics spent in that tile */
    int32_t avoid; /* tile index planned as blocked after a stall, -1 none */
} nav_state;

void nav_reset(nav_state *ns);

/* One tic of goto toward the centre of tile (tx, ty): BFS over 4-connected
 * passable tiles (game_tile_blocks) that hold no live thing (thinker_alive:
 * the centre of an enemy or barrel inside the tile) and are not ns->avoid,
 * except the goal and the player's own tile; when no such path exists, the
 * tile-only path. Steer at the next tile centre on the path with turn +
 * forward at walk speed, slowing on the final approach so the player comes
 * to rest within NAV_ARRIVE_DIST of the centre. Replans every tic; after
 * NAV_STALL_TICS forward tics in one tile, avoids the tile it steers into
 * until the player changes tile. */
int nav_goto(const game_state *gs, int tx, int ty, nav_state *ns, ticcmd *out);

/* One tic of turning toward `target` the shortest way at keyboard turn
 * speed. Returns 1 (no tic used) when within NAV_FACE_TOL. */
int nav_face(const game_state *gs, angle_t target, ticcmd *out);

/* Direction from the player to the centre of tile (tx, ty). */
angle_t nav_angle_to_tile(const game_state *gs, int tx, int ty);

/* Degrees (any integer, wrapped) to a binary angle, rounded to nearest. */
angle_t nav_deg_to_angle(int32_t deg);

#endif
