#ifndef GAME_TOPDOWN_H
#define GAME_TOPDOWN_H

/* Dev top-down view (roadmap.md `--topdown`): the only view until the 3D
 * renderer exists. Draws into a SCREEN_W x SCREEN_H index framebuffer. */

#include <stdint.h>

#include "game/state.h"

void topdown_draw(const game_state *gs, uint8_t *fb);

#endif
