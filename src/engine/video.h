#ifndef ENGINE_VIDEO_H
#define ENGINE_VIDEO_H

#include <stdint.h>

/* Native framebuffer: 320x240, 8-bit palette indices (GAME.md §2). */
enum {
    SCREEN_W = 320,
    SCREEN_H = 240,
    PALETTE_BYTES = 768
};

/* Palette test card used until the real palette exists (M1 palette issue).
 * Fills fb (SCREEN_W*SCREEN_H) with index = x/20 + 16*(y/15) and pal with a
 * 256-step grey ramp (entry i = (i,i,i)). */
void video_test_pattern(uint8_t *fb, uint8_t *pal);

#endif
