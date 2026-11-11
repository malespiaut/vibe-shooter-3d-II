#include "engine/video.h"

void video_test_pattern(uint8_t *fb, uint8_t *pal)
{
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            fb[y * SCREEN_W + x] = (uint8_t)(x / 20 + 16 * (y / 15));
    for (int i = 0; i < 256; i++) {
        pal[i * 3 + 0] = (uint8_t)i;
        pal[i * 3 + 1] = (uint8_t)i;
        pal[i * 3 + 2] = (uint8_t)i;
    }
}
