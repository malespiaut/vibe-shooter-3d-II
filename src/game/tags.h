#ifndef GAME_TAGS_H
#define GAME_TAGS_H

/* Map logic (tech.md Map: cell flags and special entities): tags, SWITCH
 * and EXIT cells, PUSHWALLs, walk-over TRIGGERs, SECRETs, LIGHTS entities
 * and DAMAGE floors. Pure game state, fixed point, hashed.
 *
 * A tag (1..255; 0 means none) fires every DOOR and PUSHWALL cell carrying
 * it (closed doors open, START_OPEN doors close, idle pushwalls slide away
 * from the player) and every LIGHTS entity carrying it (the level switches
 * from each cell's `dark` to its `dark_alt`). */

#include <stdint.h>

#include "engine/fixed.h"
#include "engine/hash.h"

typedef struct game_state game_state;

#define MAX_PUSHWALLS 64
#define PUSHWALL_SPEED (FIX_ONE / 32) /* 1 tile per 32 tics */
#define PUSHWALL_TILES 2
#define DAMAGE_PERIOD 16 /* DAMAGE cells hurt on every 16th tic */

enum { PUSHWALL_IDLE, PUSHWALL_MOVING, PUSHWALL_DONE };

/* gs->cell_fired bits: one-shot cells already used or entered. */
enum { FIRED_SWITCH = 1, FIRED_TRIGGER = 2, FIRED_SECRET = 4 };

typedef struct {
    uint8_t tx, ty; /* the tile it occupies (moving: the tile it leaves) */
    int8_t dx, dy;  /* slide direction, one axis */
    uint8_t state;  /* PUSHWALL_* */
    uint8_t moved;  /* whole tiles moved so far */
    fix_t pos;      /* 0..FIX_ONE into the next tile */
    uint8_t wall;   /* wall texture of its map cell (constant, for drawing) */
} pushwall_t;

/* Builds the pushwall list (row-major) and counts SECRET cells
 * (called by game_init after doors_init). */
void tags_init(game_state *gs);

/* Fires `tag` (no-op for 0). */
void game_fire_tag(game_state *gs, int tag);

/* BT_USE rising edge: the first non-empty tile within DOOR_USE_RANGE along
 * the view. A door: door_use(). A SWITCH cell: fires its tag once (switch
 * state on; timed doors on that tag close `param` seconds later). An EXIT
 * cell: sets gs->exit. An idle PUSHWALL: slides away from the player. */
void game_use(game_state *gs);

/* Moves every moving pushwall by one tic. */
void pushwalls_tic(game_state *gs);

/* Effects of the cell under the player's centre, after movement: TRIGGER
 * fires its tag once, SECRET counts once, DAMAGE takes `param` health on
 * every tic divisible by DAMAGE_PERIOD. */
void tags_player_cell(game_state *gs);

/* True when a pushwall occupies tile (tx, ty) (a moving one occupies both
 * the tile it leaves and the tile it enters). */
int pushwall_blocks(const game_state *gs, int tx, int ty);

/* Index of the pushwall whose current tile is (tx, ty), or -1. */
int pushwall_at(const game_state *gs, int tx, int ty);

/* The live darkness of a cell: `dark`, or `dark_alt` once lights fired. */
int game_cell_dark(const game_state *gs, int tx, int ty);

/* Appends the map-logic state to the game hash. */
void tags_hash(const game_state *gs, hash_t *h);

#endif
