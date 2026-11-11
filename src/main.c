/* Vibe Shooter 3D II entry point. M1 bootstrap: shows the palette test card
 * (in the game palette, loaded with the colormap) until the renderer exists.
 * Quits on Esc or window close. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "engine/colormap.h"
#include "engine/video.h"
#include "platform/platform.h"

static uint8_t framebuffer[SCREEN_W * SCREEN_H];
static uint8_t palette[PALETTE_BYTES];

/* Audio: no mixer yet, so the engine answers every request with silence. */
static void audio_fill(int16_t *out, int frames, void *user)
{
    (void)user;
    memset(out, 0, (size_t)frames * PLAT_AUDIO_CHANNELS * sizeof(int16_t));
}

#ifndef VS3D_ASSETS_DIR
#define VS3D_ASSETS_DIR "assets/gen"
#endif

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* Palette + light colormap, built by `cmake --build build`. */
    if (!colormap_load(VS3D_ASSETS_DIR)) {
        fprintf(stderr, "game: %s\n", colormap_error());
        return 1;
    }

    if (!platform_init("Vibe Shooter 3D II", 3)) {
        fprintf(stderr, "game: %s\n", platform_error());
        return 1;
    }
    if (!platform_audio_open(audio_fill, NULL))
        fprintf(stderr, "game: audio disabled: %s\n", platform_error());

    video_test_pattern(framebuffer, palette);
    memcpy(palette, colormap_palette(), PALETTE_BYTES); /* real game palette */

    int running = 1;
    while (running) {
        plat_event ev;
        while (platform_poll_event(&ev)) {
            if (ev.type == PEV_QUIT)
                running = 0;
            else if (ev.type == PEV_KEY_DOWN && ev.key == PKEY_ESCAPE)
                running = 0;
        }
        platform_present(framebuffer, palette);
        platform_sleep_ms(10);
    }

    platform_shutdown();
    return 0;
}
