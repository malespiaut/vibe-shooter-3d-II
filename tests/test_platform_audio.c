/* Audio wiring: under SDL_AUDIODRIVER=dummy, the platform must pull stereo
 * int16 frames from the engine callback, and shut down cleanly. */
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "platform/platform.h"

static atomic_int g_calls;
static atomic_long g_frames;

static void fill_silence(int16_t *out, int frames, void *user)
{
    assert(user == &g_calls);
    assert(frames > 0);
    memset(out, 0, (size_t)frames * PLAT_AUDIO_CHANNELS * sizeof(int16_t));
    g_calls++;
    g_frames += frames;
}

int main(void)
{
    if (!platform_audio_open(fill_silence, (void *)&g_calls)) {
        fprintf(stderr, "audio open failed: %s\n", platform_error());
        return 1;
    }
    uint64_t start = platform_ticks_ms();
    while (g_calls == 0 && platform_ticks_ms() - start < 3000)
        platform_sleep_ms(10);
    platform_audio_close();
    platform_shutdown();
    printf("audio callback calls=%d frames=%ld\n", g_calls, g_frames);
    assert(g_calls > 0);
    return 0;
}
