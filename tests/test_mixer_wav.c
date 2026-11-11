/* ctest `mixer_wav`: mixes a scripted sequence of real sound effects into a
 * WAV for evidence, and checks its length and stereo placement.
 *
 *   test_mixer_wav <assets/gen dir> <out.wav>
 *
 * Script: door_open panned left at t=0, secret_found panned right at
 * t=0.3 s (overlapping the door). Rendered in 441-frame blocks (20 ms), with
 * commands posted between blocks as the game thread would. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "engine/mixer.h"

#define BLOCK 441
#define SECRET_AT (15 * BLOCK) /* 6615 frames = 0.3 s */
#define MAX_FRAMES (MIXER_RATE * 4)

static int16_t g_pcm[MAX_FRAMES * 2];

static void put16(FILE *f, unsigned v)
{
    fputc((int)(v & 0xff), f);
    fputc((int)((v >> 8) & 0xff), f);
}

static void put32(FILE *f, unsigned long v)
{
    put16(f, (unsigned)(v & 0xffff));
    put16(f, (unsigned)(v >> 16));
}

static void write_wav(const char *path, const int16_t *pcm, int frames)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    unsigned long data = (unsigned long)frames * 4;
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + data);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);                    /* PCM */
    put16(f, 2);                    /* stereo */
    put32(f, MIXER_RATE);
    put32(f, MIXER_RATE * 4);       /* byte rate */
    put16(f, 4);                    /* block align */
    put16(f, 16);                   /* bits */
    fwrite("data", 1, 4, f);
    put32(f, data);
    for (long i = 0; i < (long)frames * 2; i++)
        put16(f, (unsigned)(uint16_t)pcm[i]);
    assert(fclose(f) == 0);
}

/* Sum of squares per channel over frames [a, b). */
static void energy(int a, int b, int64_t *l, int64_t *r)
{
    *l = *r = 0;
    for (int i = a; i < b; i++) {
        *l += (int64_t)g_pcm[2 * i] * g_pcm[2 * i];
        *r += (int64_t)g_pcm[2 * i + 1] * g_pcm[2 * i + 1];
    }
}

static bool any_active(void)
{
    for (int i = 0; i < MIXER_VOICES; i++) {
        mixer_voice_info v;
        mixer_get_voice(i, &v);
        if (v.active)
            return true;
    }
    return false;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    mixer_init(argv[1]);
    mixer_sound *door = mixer_load_snd("door_open");
    mixer_sound *secret = mixer_load_snd("secret_found");
    if (!door || !secret) {
        fprintf(stderr, "mixer_wav: %s\n", bast_error());
        return 1;
    }
    assert(mixer_load_snd("door_open") == door); /* cached by name */
    int door_len = (int)mixer_sound_length(door);
    int secret_len = (int)mixer_sound_length(secret);
    assert(door_len > SECRET_AT); /* the two overlap */

    enum { DOOR_SRC = 1, SECRET_SRC = 2 };
    int frames = 0;
    assert(mixer_start(door, DOOR_SRC, MIXER_VOL_MAX, -3));
    do {
        if (frames == SECRET_AT)
            assert(mixer_start(secret, SECRET_SRC, MIXER_VOL_MAX, 3));
        assert(frames + BLOCK <= MAX_FRAMES);
        mixer_render(g_pcm + 2 * frames, BLOCK);
        frames += BLOCK;
    } while (frames <= SECRET_AT || any_active());

    /* Length: everything up to the end of secret_found, rounded up to a block. */
    int expect_end = SECRET_AT + secret_len;
    int expect_frames = (expect_end + BLOCK - 1) / BLOCK * BLOCK;
    assert(frames == expect_frames);
    for (int i = expect_end; i < frames; i++)
        assert(g_pcm[2 * i] == 0 && g_pcm[2 * i + 1] == 0);

    int64_t l1, r1, l2, r2, l3, r3;
    energy(0, SECRET_AT, &l1, &r1);           /* door only: left */
    energy(SECRET_AT, door_len, &l2, &r2);    /* overlap */
    energy(door_len, expect_end, &l3, &r3);   /* secret only: right */
    printf("mixer_wav: %d frames (%d.%03d s); door %d, secret %d at %d\n", frames,
           frames / MIXER_RATE, frames % MIXER_RATE * 1000 / MIXER_RATE, door_len,
           secret_len, SECRET_AT);
    printf("energy L/R  door-only %lld/%lld  overlap %lld/%lld  secret-only %lld/%lld\n",
           (long long)l1, (long long)r1, (long long)l2, (long long)r2, (long long)l3,
           (long long)r3);
    /* Pan 3: far side gets gain 16/64, i.e. 1/16 of the energy. */
    assert(l1 > 0 && l1 > 10 * r1 && r1 > 0);
    assert(r3 > 0 && r3 > 10 * l3 && l3 > 0);
    assert(l2 > 0 && r2 > 0);

    write_wav(argv[2], g_pcm, frames);
    printf("wrote %s\n", argv[2]);
    mixer_shutdown();
    return 0;
}
