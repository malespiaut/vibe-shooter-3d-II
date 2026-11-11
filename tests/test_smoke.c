/* Smoke test: the build links the engine library and the test card is sane. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "engine/video.h"

static uint8_t fb[SCREEN_W * SCREEN_H];
static uint8_t pal[PALETTE_BYTES];

int main(void)
{
    assert(sizeof(fb) == 76800);
    video_test_pattern(fb, pal);

    /* Corners and cell boundaries follow index = x/20 + 16*(y/15). */
    assert(fb[0] == 0);
    assert(fb[19] == 0);
    assert(fb[20] == 1);
    assert(fb[319] == 15);
    assert(fb[15 * SCREEN_W] == 16);
    assert(fb[239 * SCREEN_W + 319] == 255);

    /* Every index 0..255 appears exactly 20*15 times. */
    int count[256] = {0};
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
        count[fb[i]]++;
    for (int i = 0; i < 256; i++)
        assert(count[i] == 300);

    /* Grey ramp: 0 black, 255 white, monotonic. */
    assert(pal[0] == 0 && pal[1] == 0 && pal[2] == 0);
    assert(pal[765] == 255 && pal[766] == 255 && pal[767] == 255);
    for (int i = 1; i < 256; i++) {
        assert(pal[i * 3] == pal[i * 3 + 1] && pal[i * 3] == pal[i * 3 + 2]);
        assert(pal[i * 3] > pal[(i - 1) * 3]);
    }

    printf("smoke ok\n");
    return 0;
}
