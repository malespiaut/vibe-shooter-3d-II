#ifndef GAME_FLOW_H
#define GAME_FLOW_H

/* Front-end flow (docs/design/hud.md Flow): title, main menu, difficulty,
 * controls, play and the pause menu, as a screen state machine driven once
 * per tic by one menu key. Deterministic: no clock, no RNG (nothing here is
 * random; anything that becomes random uses rng_ui, never rng_game). The
 * flow state is part of the run, not of the gameplay hash: game_hash() only
 * sees the game_state, which this module ticks while the screen is PLAY.
 *
 *   title --any key--> menu --New Game--> skill --Enter--> play
 *   play --Esc--> pause (Resume, Restart Level, Controls, Quit to Title)
 *
 * The intro screens, intermission, ending and credits are issue #102. */

#include <stdint.h>

#include "bast.h"
#include "game/state.h"

enum flow_screen {
    FLOW_TITLE,
    FLOW_MENU,
    FLOW_SKILL,
    FLOW_CONTROLS,
    FLOW_PLAY,
    FLOW_PAUSE,
    FLOW_SCREEN_COUNT
};

/* One menu key per tic (demo `key` lines, window key presses). */
enum flow_key { FKEY_NONE, FKEY_UP, FKEY_DOWN, FKEY_ENTER, FKEY_ESC, FKEY_ANY };

/* Sound requested by the last flow_tic (the caller plays it if it has audio). */
enum flow_sound { FSND_NONE, FSND_MOVE, FSND_SELECT };

/* Main menu and pause menu items, in screen order. */
enum { MENU_NEW_GAME, MENU_CONTROLS, MENU_QUIT, MENU_COUNT };
enum { PAUSE_RESUME, PAUSE_RESTART, PAUSE_CONTROLS, PAUSE_QUIT_TITLE, PAUSE_COUNT };
#define SKILL_COUNT 3
#define FLOW_DEFAULT_SKILL 2 /* Tomcat */

#define FLOW_BLINK_TICS 35 /* PRESS ANY KEY: 35 tics on, 35 off */

typedef struct {
    int screen;   /* FLOW_* */
    int cursor;   /* item index on MENU, SKILL, PAUSE */
    int back;     /* screen CONTROLS returns to (MENU or PAUSE) */
    uint32_t timer; /* tics spent on the current screen */
    int sound;    /* FSND_* from the last flow_tic */
} flow_t;

/* What a level start needs; owned by the caller. `entry` receives a copy of
 * the state at level start, which Restart Level restores. */
typedef struct {
    game_state *gs;
    game_state *entry;
    const bast_map *map;
    int level;
    uint32_t seed;
} flow_world;

/* Starts on `screen` (FLOW_TITLE, or FLOW_PLAY for `start level`, --warp,
 * --map). On FLOW_PLAY, w->gs must already be initialised: it is copied to
 * w->entry. */
void flow_init(flow_t *f, int screen, flow_world *w);

enum {
    FLOW_TICKED = 1, /* game_tic ran this tic */
    FLOW_EXIT = 2,   /* main menu Quit chosen */
};
/* One tic: applies `key`, then runs game_tic(w->gs, cmd) if the screen is
 * (still) PLAY and the key did not just change it. Esc in play opens pause
 * without a game tic. New Game starts the level at the chosen skill
 * (game_init with w->map, w->level, w->seed). Returns FLOW_* flags. */
int flow_tic(flow_t *f, int key, flow_world *w, const ticcmd *cmd);

/* PRESS ANY KEY visible on this tic (title only). */
int flow_title_blink_on(const flow_t *f);

/* Lower-case screen name ("title", "menu", ...), NULL if out of range. */
const char *flow_screen_name(int screen);
/* Screen for a name, -1 if unknown. */
int flow_screen_from_name(const char *name);

/* ---- drawing (reads flow state only) ---- */

typedef struct flow_gfx flow_gfx;
/* Loads ui/title.tex, hud/menu_ankh.tex and fonts menu + small from `dir`
 * (assets/gen). NULL on failure, message in flow_gfx_error(). */
flow_gfx *flow_gfx_load(const char *dir);
const char *flow_gfx_error(void);
void flow_gfx_free(flow_gfx *g);

/* Draws the current screen. For PLAY nothing is drawn; for PAUSE `fb` must
 * already hold the frozen 3D view, which is darkened in place. */
void flow_draw(const flow_t *f, const flow_gfx *g, uint8_t *fb);

#endif
