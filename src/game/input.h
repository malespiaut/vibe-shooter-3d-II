/* Window-mode controls to ticcmd (hud.md Controls). The platform layer
 * fills an input_state from held keys and mouse buttons; this builds the
 * same ticcmd the demo commands do, so --record captures it unchanged. */
#ifndef VS3D_GAME_INPUT_H
#define VS3D_GAME_INPUT_H

#include "game/state.h"

#define INPUT_MOUSE_TURN 40 /* angle units per pixel of mouse X motion */

typedef struct {
    int forward, back;            /* W/S, up/down arrows */
    int strafe_left, strafe_right; /* A/D */
    int turn_left, turn_right;    /* left/right arrows */
    int run;                      /* Shift */
    int use;                      /* E, Space */
    int fire;                     /* left mouse button, Ctrl */
    int mouse_dx;                 /* relative X motion since last tic */
    int weapon_slot;              /* 1-7 for one tic after the key press, else 0 */
} input_state;

void input_build_ticcmd(const input_state *in, ticcmd *cmd);

/* Weapon slot (1-7) for a key code (ASCII digit), else 0. */
int input_weapon_key(int key);

#endif
