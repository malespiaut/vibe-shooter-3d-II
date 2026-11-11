#ifndef GAME_HUD_H
#define GAME_HUD_H

/* In-game HUD (hud.md "Status bar", "Other HUD elements"): status bar,
 * pickup messages, view border flash and the cat face. Render side only:
 * reads game state (including its unhashed event ring), never writes it.
 * Timers live in hud_state, advanced once per game tic by hud_tic(). */

#include <stdint.h>

#include "game/hud_face.h"
#include "game/state.h"

#define HUD_BAR_Y 208
#define HUD_MSG_TICS 105
#define HUD_MSG_LINES 2
#define HUD_MSG_LEN 48
#define HUD_FLASH_HIT_TICS 4
#define HUD_FLASH_PICKUP_TICS 3
#define HUD_FLASH_W 2
#define HUD_IDX_FB_GOLD 242 /* fb_gold (catlantis.gpl), powerup bar */

typedef struct {
    char text[HUD_MSG_LEN];
    int tics; /* left; 0 = empty */
} hud_message;

typedef struct {
    hud_face face;
    hud_message msg[HUD_MSG_LINES]; /* [0] is the older line */
    int flash_hit, flash_pickup;    /* tics left */
    uint32_t seen_seq;              /* game_state.event_seq already read */
    int god;                        /* dev flag: god face */
} hud_state;

/* Loads fonts and HUD art from `assets_dir` (fonts/NAME.tex, hud/NAME.tex;
 * hud/faces/NAME.tex optional: a missing face shows the empty cartouche).
 * Returns 1, or 0 with hud_error() set. */
int hud_init(const char *assets_dir);
void hud_shutdown(void);
const char *hud_error(void);

/* Starts reading `gs` from its current event (no stale messages). */
void hud_state_init(hud_state *h, const game_state *gs, int god);
/* One game tic: ages timers, then takes the events gs posted this tic. */
void hud_tic(hud_state *h, const game_state *gs);
/* Draws the border flash over the view, the messages and the status bar
 * (rows HUD_BAR_Y..239). */
void hud_draw(uint8_t *fb, const hud_state *h, const game_state *gs);

/* "PICKED UP A FISH SNACK." for pickup entity `type`, NULL if none. */
const char *hud_pickup_message(int type);

#endif
