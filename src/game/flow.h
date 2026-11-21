#ifndef GAME_FLOW_H
#define GAME_FLOW_H

/* Front-end flow (docs/design/hud.md Flow): title, main menu, difficulty,
 * controls, play and the pause menu, as a screen state machine driven once
 * per tic by one menu key. Deterministic: no clock, no RNG (nothing here is
 * random; anything that becomes random uses rng_ui, never rng_game). The
 * flow state is part of the run, not of the gameplay hash: game_hash() only
 * sees the game_state, which this module ticks while the screen is PLAY.
 *
 *   title --any key--> menu --New Game--> skill --Enter--> intro (2+) --> play
 *   play --Esc--> pause (Resume, Restart Level, Controls, Quit to Title)
 *   play --exit--> tally --> (level < 8) story --> play level N+1
 *                            (level 8) ending (2+) --> credits --> title
 *
 * Text screens (intro, story, ending) advance a page per key. A level change
 * carries health, armour, weapons and ammo (game_carry), not keys or
 * powerups (items.md). */

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
    FLOW_INTRO,
    FLOW_TALLY,
    FLOW_STORY,
    FLOW_ENDING,
    FLOW_CREDITS,
    FLOW_SCREEN_COUNT
};

/* One menu key per tic (demo `key` lines, window key presses). */
enum flow_key { FKEY_NONE, FKEY_UP, FKEY_DOWN, FKEY_ENTER, FKEY_ESC, FKEY_ANY };

/* Sound requested by the last flow_tic (the caller plays it if it has audio). */
enum flow_sound { FSND_NONE, FSND_MOVE, FSND_SELECT, FSND_TALLY_TICK, FSND_TALLY_DONE };

/* Main menu and pause menu items, in screen order. */
enum { MENU_NEW_GAME, MENU_CONTROLS, MENU_QUIT, MENU_COUNT };
enum { PAUSE_RESUME, PAUSE_RESTART, PAUSE_CONTROLS, PAUSE_QUIT_TITLE, PAUSE_COUNT };
#define SKILL_COUNT 3
#define FLOW_DEFAULT_SKILL 2 /* Tomcat */

#define FLOW_BLINK_TICS 35 /* PRESS ANY KEY: 35 tics on, 35 off */
#define FLOW_LAST_LEVEL 8   /* its exit leads to the ending */

/* Credits (story.md): line pitch 14 px (the menu font's cell height), they
 * start below the screen and scroll up 1 px every 2 tics until the last
 * line has left the top, then the title. */
#define FLOW_CREDITS_LINE_H 14
int flow_credits_height(void);    /* lines x FLOW_CREDITS_LINE_H */
uint32_t flow_credits_tics(void); /* 2 * (height + 240) */

/* Intermission tally (hud.md Flow). Lines count up one after another, 1 per
 * tic: KILLS %, ITEMS %, SECRETS found, then TIME in seconds. */
enum { TALLY_KILLS, TALLY_ITEMS, TALLY_SECRETS, TALLY_TIME, TALLY_LINES };
typedef struct {
    int32_t target[TALLY_LINES]; /* final values */
    int32_t shown[TALLY_LINES];  /* counted so far */
    int32_t secret_total;
    int32_t par;                 /* seconds, from the map header */
    int done;                    /* every line at its target */
    char name[40];               /* level name, upper case */
} tally_t;

/* found/total in percent, rounded down; 0/0 is 100%. */
int32_t tally_percent(int32_t found, int32_t total);
/* Seconds as m:ss ("4:12"). */
void tally_format_time(int32_t seconds, char *buf, int cap);
/* Fills targets from a finished level, everything shown at 0. */
void tally_init(tally_t *t, const game_state *gs);
/* One counting tic: 1 if a value moved (tick sound), 0 if done. */
int tally_step(tally_t *t);

typedef struct {
    int screen;   /* FLOW_* */
    int cursor;   /* item index on MENU, SKILL, PAUSE */
    int back;     /* screen CONTROLS returns to (MENU or PAUSE) */
    uint32_t timer; /* tics spent on the current screen */
    int sound;    /* FSND_* from the last flow_tic */
    int page;     /* page on INTRO, STORY, ENDING */
    int done_level; /* level the tally and story are for */
    tally_t tally;
} flow_t;

/* What a level start needs; owned by the caller. `entry` receives a copy of
 * the state at level start, which Restart Level restores. */
typedef struct {
    game_state *gs;
    game_state *entry;
    const bast_map *map;
    int level;
    uint32_t seed;
    /* Loads the map of `level` for the next level (story -> play). The
     * returned map must stay valid until the next call; NULL: no such
     * level, the flow returns to the title. May be NULL (no progression). */
    const bast_map *(*load_level)(void *ctx, int level);
    void *ctx;
} flow_world;

/* Starts on `screen` (FLOW_TITLE, or FLOW_PLAY for `start level`, --warp,
 * --map). On FLOW_PLAY, w->gs must already be initialised: it is copied to
 * w->entry. */
void flow_init(flow_t *f, int screen, flow_world *w);

enum {
    FLOW_TICKED = 1, /* game_tic ran this tic */
    FLOW_EXIT = 2,   /* main menu Quit chosen */
    FLOW_NEWLEVEL = 4, /* w->gs was (re)started: New Game, Restart Level, next level */
    FLOW_LEVELDONE = 8, /* the level's exit was used: the tally starts */
};
/* One tic: applies `key`, then runs game_tic(w->gs, cmd) if the screen is
 * (still) PLAY and the key did not just change it. Esc in play opens pause
 * without a game tic. New Game starts the level at the chosen skill
 * (game_init with w->map, w->level, w->seed). Returns FLOW_* flags. */
int flow_tic(flow_t *f, int key, flow_world *w, const ticcmd *cmd);

/* Text of the current page of INTRO, STORY or ENDING (wrapped, '\n'
 * separated); 0 if the screen has no text. */
int flow_page_text(const flow_t *f, char *out, int cap);

/* PRESS ANY KEY visible on this tic (title only). */
int flow_title_blink_on(const flow_t *f);

/* Lower-case screen name ("title", "menu", ...), NULL if out of range. */
const char *flow_screen_name(int screen);
/* Screen for a name, -1 if unknown. */
int flow_screen_from_name(const char *name);

/* ---- drawing (reads flow state only) ---- */

typedef struct flow_gfx flow_gfx;
/* Loads ui/title.tex, ui/papyrus_panel.tex, ui/tally_panel.tex,
 * hud/menu_ankh.tex and fonts menu + small from `dir`
 * (assets/gen). NULL on failure, message in flow_gfx_error(). */
flow_gfx *flow_gfx_load(const char *dir);
const char *flow_gfx_error(void);
void flow_gfx_free(flow_gfx *g);

/* Draws the current screen. For PLAY nothing is drawn; for PAUSE `fb` must
 * already hold the frozen 3D view, which is darkened in place. */
void flow_draw(const flow_t *f, const flow_gfx *g, uint8_t *fb);

#endif
