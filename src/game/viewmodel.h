#ifndef GAME_VIEWMODEL_H
#define GAME_VIEWMODEL_H

/* First-person weapon view-model (weapons.md: View-model animation frames,
 * Behaviour notes, Bob). Render side only: reads the player's weapon state
 * (game/weapon.h weapons_t) and momentum, never writes game state, so demo
 * hashes do not depend on it.
 *
 * Placement: frame vm<N>_<frame> (assets/gen/viewmodels, origin bottom-
 * centre) with its bottom-centre at (VIEW_W/2 + bob x, VIEW_H + bob y +
 * drop), clipped to the 320x208 view.
 *
 * Bob: a figure-eight from the fine trig table, x = 6 sin(p), y = 4 + 4
 * sin(2p) (y down, so the bottom edge never lifts above the view's bottom
 * row and no gap opens under the art), both scaled by the player's speed /
 * walk speed (capped at 1): at rest the sprite sits flush. Rounded to the
 * nearest pixel.
 *
 * Switch: lower 5 + raise 5 (WEAPON_SWITCH_TICS). With s = switch_tics,
 * the old weapon is drawn s > 5 lowered by h * (10 - s) / 5, the new one
 * s <= 5 lowered by h * s / 5 (fully down at the swap tic, s == 5).
 *
 * Fire frames: elapsed e = interval - refire_tics since the shot (0 on the
 * fire tic); each weapon has a list of (frame, tics) after which it shows
 * idle (see FRAMES in viewmodel.c). Pistol: fire0 2 tics, fire1 4 tics.
 *
 * Flash: while flash_tics > 0 the whole view is drawn VM_FLASH_ROWS
 * colormap rows brighter (render_set_light_shift(-4)). */

#include <stdint.h>

#include "engine/angle.h"
#include "engine/fixed.h"
#include "game/state.h"
#include "game/weapon.h"

#define VM_BOB_X 6
#define VM_BOB_Y 4
#define VM_FLASH_ROWS 4

/* Bob offset in px for `phase` at `speed` (16.16 units per tic). */
void viewmodel_bob(angle_t phase, fix_t speed, int *dx, int *dy);

/* Pixels the sprite of height `h` is lowered by at `switch_tics`. */
int viewmodel_drop(int h, int switch_tics);

/* Weapon slot drawn: the old one while lowering, then the new one. */
int viewmodel_weapon(const weapons_t *w);

/* Frame name ("idle", "fire0", ...) of weapon `wp` `elapsed` tics after a
 * shot. Returns
 * "idle" when elapsed is past the sequence or negative. */
const char *viewmodel_frame(int wp, int elapsed);

/* Frame name for the current weapon state (viewmodel_weapon's slot). */
const char *viewmodel_frame_of(const weapons_t *w);

/* Light shift for render_set_light_shift(): -VM_FLASH_ROWS while the
 * muzzle flash is on, else 0. */
int viewmodel_light_shift(const weapons_t *w);

/* Loads every vm<N>_<frame>.tex of weapons 1-7 from `assets_dir`/
 * viewmodels. A weapon with missing frames prints one warning to stderr
 * and its missing frames are skipped when drawn. Always returns 1. */
int viewmodel_init(const char *assets_dir);
void viewmodel_shutdown(void);

/* Draws the view-model for `gs` into the view rows of `fb` (nothing when
 * the player is dead or the frame is not loaded). */
void viewmodel_draw(uint8_t *fb, const game_state *gs);

#endif
