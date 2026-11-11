#ifndef GAME_DOOR_H
#define GAME_DOOR_H

/* Doors (tech.md Map: cell flags DOOR, ONEWAY, START_OPEN, door `param`).
 * One door record per DOOR cell, created at game_init in row-major order
 * (y, then x, from the south-west). Pure game state: rendering reads `pos`.
 *
 * Door cell `param`: bits 0-1 key (0 none, 1 green, 2 blue, 3 violet),
 * bits 2-3 mode (DOOR_MODE_*), bits 4-5 ONEWAY side (DOOR_SIDE_*: the side
 * of the door the player must stand on to open it). */

#include <stdint.h>

#include "engine/fixed.h"

typedef struct game_state game_state;

#define MAX_DOORS 256
#define DOOR_SPEED (FIX_ONE / 32) /* 32 tics to open or close fully */
#define DOOR_WAIT_TICS 140        /* mode 0: open time before closing */
#define DOOR_USE_RANGE (FIX_ONE * 3 / 4)
#define DOOR_TIMER_DUE (-1)

enum { DOOR_CLOSED, DOOR_OPENING, DOOR_OPEN, DOOR_CLOSING };

enum {
    DOOR_MODE_AUTO = 0,   /* closes DOOR_WAIT_TICS after fully open */
    DOOR_MODE_STAY = 1,   /* stays open */
    DOOR_MODE_SWITCH = 2, /* use does nothing; door_activate() only */
    DOOR_MODE_TIMED = 3,  /* like SWITCH, then closes the switch's param
                           * seconds after the switch fired */
};

enum { DOOR_SIDE_NORTH = 0, DOOR_SIDE_EAST = 1, DOOR_SIDE_SOUTH = 2, DOOR_SIDE_WEST = 3 };

/* Player key bits (player_t.keys). */
enum { KEY_GREEN = 1, KEY_BLUE = 2, KEY_VIOLET = 4 };

#define DOOR_KEY(param) ((param) & 3)
#define DOOR_MODE(param) (((param) >> 2) & 3)
#define DOOR_ONEWAY_SIDE(param) (((param) >> 4) & 3)

typedef struct {
    uint8_t tx, ty;
    uint8_t state;   /* DOOR_CLOSED.. */
    uint8_t param;   /* the cell's param */
    uint16_t flags;  /* the cell's flags (ONEWAY, START_OPEN) */
    fix_t pos;       /* 0 closed .. FIX_ONE open */
    int32_t timer;   /* mode AUTO, DOOR_OPEN: tics left before closing;
                      * mode TIMED: tics left of the switch delay (counting
                      * from the switch), DOOR_TIMER_DUE once elapsed,
                      * 0 = no delay running (stays open) */
} door_t;

/* Builds the door list from the map (called by game_init). START_OPEN
 * doors begin fully open and stay open until door_activate() closes them. */
void doors_init(game_state *gs);

/* Index of the door at tile (tx, ty), or -1. */
int door_at(const game_state *gs, int tx, int ty);

/* True when the door does not block (fully open). */
int door_passable(const door_t *d);

/* Player use: opens a closed or closing door when its mode allows use, the
 * player owns its key, and (ONEWAY) the player stands on its side.
 * Returns 1 when the door starts opening. */
int door_use(game_state *gs, int idx);

/* Enemy (game/ai.c): opens a closed or closing door with no key, mode
 * AUTO or STAY, and not ONEWAY. Returns 1 when it starts opening. */
int door_monster_open(game_state *gs, int idx);

/* Tag/switch activation (the tag system calls this): an open or opening
 * START_OPEN door closes; any other closed or closing door opens, whatever
 * its mode, key or side. */
void door_activate(game_state *gs, int idx);

/* door_activate(), and a TIMED-mode door also starts its close delay of
 * `close_tics` (> 0; from its switch's param). */
void door_activate_timed(game_state *gs, int idx, int32_t close_tics);

/* Moves every door by one tic. A door never starts closing on the player
 * or a live enemy, and re-opens if one is inside a closing door. */
void doors_tic(game_state *gs);

/* BT_USE: the first non-empty tile within DOOR_USE_RANGE along the view
 * direction (grid DDA from the player's position). Returns its door index
 * if it is a door, else -1. */
int door_use_target(const game_state *gs);

/* The same trace for any use target: the first tile within DOOR_USE_RANGE
 * that is a door or blocks. Returns 1 with the tile and the unit step
 * (sx, sy) that entered it (the direction away from the player), else 0. */
int use_trace(const game_state *gs, int *tx, int *ty, int *sx, int *sy);

#endif
