/* ctest `music_render`: renders a song through the mixer twice and checks
 * that the output is byte-identical, non-silent in every 5 s window, and that
 * after the order list wraps the audio repeats the first pass from the loop
 * point.
 *
 *   test_music_render <title.vmus> */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/mixer.h"
#include "engine/music.h"

#define BLOCK 441 /* odd block size: tic and mixer chunk edges never align */
#define CMP_SECONDS 10

static int16_t *render(const music_song *s, long frames)
{
    int16_t *pcm = malloc(sizeof(int16_t) * 2 * (size_t)frames);
    assert(pcm);
    mixer_init(NULL);
    music_attach();
    music_play(s);
    for (long done = 0; done < frames;) {
        int n = frames - done < BLOCK ? (int)(frames - done) : BLOCK;
        mixer_render(pcm + 2 * done, n);
        done += n;
    }
    return pcm;
}

/* Rising zero crossings over one second of a held note, after 1 tic. */
static int measure_hz(const char *wave, const char *note)
{
    static char text[8192];
    char *p = text;
    p += sprintf(p, "vmus 1\ntempo 4\ninst 01 %s\npattern 0\n", wave);
    for (int r = 0; r < MUSIC_ROWS; r++)
        p += sprintf(p, "%s %s .. ... | --- .. .. ... | --- .. .. ... | --- .. .. ...\n",
                     r == 0 ? note : "---", r == 0 ? "01" : "..");
    sprintf(p, "order 0\n");
    char err[200];
    music_song *s = music_parse(text, err, sizeof err);
    assert(s);
    long frames = MUSIC_TIC_SAMPLES + MIXER_RATE;
    int16_t *pcm = render(s, frames);
    int n = 0;
    for (long i = MUSIC_TIC_SAMPLES; i + 1 < frames; i++)
        if (pcm[2 * i] < 0 && pcm[2 * i + 2] >= 0)
            n++;
    free(pcm);
    music_free(s);
    return n;
}

int main(int argc, char **argv)
{
    assert(argc == 2);

    /* Tuning: A-4 = 440 Hz, A-2 = 110 Hz (12-TET table, A4 = 440). */
    int a4 = measure_hz("triangle", "A-4"), a2 = measure_hz("square", "A-2");
    int c5 = measure_hz("triangle", "C-5");
    printf("tuning: A-4 %d Hz, A-2 %d Hz, C-5 %d Hz\n", a4, a2, c5);
    assert(a4 >= 439 && a4 <= 441);
    assert(a2 >= 109 && a2 <= 111);
    assert(c5 >= 522 && c5 <= 524); /* 523.25 */

    char err[256];
    music_song *s = music_load(argv[1], err, sizeof err);
    if (!s)
        fprintf(stderr, "%s\n", err);
    assert(s);

    long pass = (long)music_order_start(s, s->norder);
    long loop_at = (long)music_order_start(s, s->loop);
    long frames = pass + (long)CMP_SECONDS * MIXER_RATE;
    printf("first pass %ld frames (%ld.%02ld s), loop to order %d at %ld frames\n", pass,
           pass / MIXER_RATE, pass % MIXER_RATE * 100 / MIXER_RATE, s->loop, loop_at);
    assert(pass >= 45L * MIXER_RATE); /* 45 s or more before looping */
    assert(pass - loop_at >= (long)CMP_SECONDS * MIXER_RATE);

    int16_t *a = render(s, frames);
    int16_t *b = render(s, frames);
    assert(memcmp(a, b, sizeof(int16_t) * 2 * (size_t)frames) == 0);
    printf("ok: two renders byte-identical (%ld frames)\n", frames);

    /* Non-silent: mean absolute level per 5 s window. */
    long win = 5L * MIXER_RATE;
    for (long w = 0; w + win <= frames; w += win) {
        int64_t sum = 0;
        int peak = 0;
        for (long i = 2 * w; i < 2 * (w + win); i++) {
            int v = a[i] < 0 ? -a[i] : a[i];
            sum += v;
            if (v > peak)
                peak = v;
        }
        long mean = (long)(sum / (2 * win));
        printf("window %3ld-%3ld s: mean |x| %5ld, peak %5d\n", w / MIXER_RATE,
               (w + win) / MIXER_RATE, mean, peak);
        assert(mean > 300);
    }

    /* Loop: the block right after the wrap equals the first pass at the loop
     * point, and differs from what was playing just before (a real jump). */
    size_t cmp = sizeof(int16_t) * 2 * (size_t)CMP_SECONDS * MIXER_RATE;
    assert(memcmp(a + 2 * pass, a + 2 * loop_at, cmp) == 0);
    if (loop_at != 0 || s->norder > 1)
        assert(memcmp(a + 2 * pass, a + 2 * (pass - (long)CMP_SECONDS * MIXER_RATE), cmp) != 0);
    printf("ok: %d s after the loop match the first pass from order %d\n", CMP_SECONDS, s->loop);

    /* music_stop silences the music channels. */
    mixer_init(NULL);
    music_attach();
    music_play(s);
    int16_t blk[2 * BLOCK];
    mixer_render(blk, BLOCK);
    music_stop();
    mixer_render(blk, BLOCK);
    for (int i = 0; i < 2 * BLOCK; i++)
        assert(blk[i] == 0);
    printf("ok: music_stop silences\n");

    free(a);
    free(b);
    music_free(s);
    return 0;
}
