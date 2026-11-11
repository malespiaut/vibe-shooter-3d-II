/* ctest `music_tracks`: checks the level and intermission songs
 * (docs/design/audio.md, "Music") against their contract.
 *
 *   test_music_tracks <song.vmus> <min_s> <max_s> [<song.vmus> <min_s> <max_s> ...]
 *
 * For each song: it parses; the first pass (start to the loop jump) lasts
 * min_s..max_s seconds; the loop target starts a note on all 4 channels (so
 * the wrap restarts cleanly); the full mix through the mixer never reaches
 * full scale; and each channel played alone is audible over the loop. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/mixer.h"
#include "engine/music.h"

#define BLOCK 441
#define FULL_SCALE 32767

/* Renders `frames` stereo frames; returns peak |x| and fills *sum with the
 * sum of |x| over both channels. */
static int render(const music_song *s, long frames, int64_t *sum)
{
    int16_t buf[2 * BLOCK];
    int peak = 0;
    *sum = 0;
    mixer_init(NULL);
    music_attach();
    music_play(s);
    for (long done = 0; done < frames;) {
        int n = frames - done < BLOCK ? (int)(frames - done) : BLOCK;
        mixer_render(buf, n);
        for (int i = 0; i < 2 * n; i++) {
            int v = buf[i] < 0 ? -buf[i] : buf[i];
            *sum += v;
            if (v > peak)
                peak = v;
        }
        done += n;
    }
    music_stop();
    return peak;
}

static void check_song(const char *path, int min_s, int max_s)
{
    char err[256];
    music_song *s = music_load(path, err, sizeof err);
    if (!s)
        fprintf(stderr, "%s: %s\n", path, err);
    assert(s);

    long pass = (long)music_order_start(s, s->norder);
    printf("%s: tempo %d, %d patterns in order, loop to %d, first pass %ld.%02ld s\n", path,
           s->tempo, s->norder, s->loop, pass / MIXER_RATE, pass % MIXER_RATE * 100 / MIXER_RATE);
    assert(pass >= (long)min_s * MIXER_RATE);
    assert(pass <= (long)max_s * MIXER_RATE);

    const music_cell *row0 = s->pat[s->order[s->loop]][0];
    for (int c = 0; c < MUSIC_CHANNELS; c++) {
        if (row0[c].note == MUSIC_NOTE_NONE || row0[c].note == MUSIC_NOTE_OFF)
            fprintf(stderr, "%s: loop target row 0 channel %d has no note\n", path, c);
        assert(row0[c].note != MUSIC_NOTE_NONE && row0[c].note != MUSIC_NOTE_OFF);
    }

    int64_t sum;
    int peak = render(s, pass, &sum);
    printf("  mix: peak %d (%d%% of full scale), mean |x| %ld\n", peak, peak * 100 / FULL_SCALE,
           (long)(sum / (2 * pass)));
    assert(peak < FULL_SCALE);

    music_song *solo = malloc(sizeof *solo);
    assert(solo);
    for (int c = 0; c < MUSIC_CHANNELS; c++) {
        memcpy(solo, s, sizeof *solo);
        for (int p = 0; p < MUSIC_MAX_PATTERNS; p++)
            for (int r = 0; r < MUSIC_ROWS; r++)
                for (int o = 0; o < MUSIC_CHANNELS; o++)
                    if (o != c)
                        solo->pat[p][r][o] = (music_cell){.vol = MUSIC_VOL_NONE};
        int cp = render(solo, pass, &sum);
        long mean = (long)(sum / (2 * pass));
        printf("  channel %d alone: peak %d, mean |x| %ld\n", c, cp, mean);
        assert(cp > 1000);
        assert(mean > 50);
    }
    free(solo);
    music_free(s);
}

int main(int argc, char **argv)
{
    assert(argc >= 4 && (argc - 1) % 3 == 0);
    for (int i = 1; i < argc; i += 3)
        check_song(argv[i], atoi(argv[i + 1]), atoi(argv[i + 2]));
    printf("ok: %d songs\n", (argc - 1) / 3);
    return 0;
}
