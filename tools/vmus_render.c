/* vmus_render: renders a .vmus song through the engine's mixer and music
 * sequencer into a 22050 Hz stereo 16-bit WAV (evidence and listening).
 *
 *   vmus_render <song.vmus> <out.wav> [seconds]   (default 60)
 *
 * Prints the song's first-pass length (start to the loop jump) and the peak. */
#include <stdio.h>
#include <stdlib.h>

#include "engine/mixer.h"
#include "engine/music.h"

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

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: vmus_render <song.vmus> <out.wav> [seconds]\n");
        return 2;
    }
    int seconds = argc == 4 ? atoi(argv[3]) : 60;
    if (seconds < 1 || seconds > 1200) {
        fprintf(stderr, "vmus_render: seconds must be 1..1200\n");
        return 2;
    }
    char err[256];
    music_song *song = music_load(argv[1], err, sizeof err);
    if (!song) {
        fprintf(stderr, "vmus_render: %s\n", err);
        return 1;
    }
    FILE *f = fopen(argv[2], "wb");
    if (!f) {
        fprintf(stderr, "vmus_render: cannot write %s\n", argv[2]);
        return 1;
    }
    long frames = (long)seconds * MIXER_RATE;
    unsigned long data = (unsigned long)frames * 4;
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + data);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 2);
    put32(f, MIXER_RATE);
    put32(f, MIXER_RATE * 4);
    put16(f, 4);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, data);

    mixer_init(NULL);
    music_attach();
    music_play(song);
    int16_t buf[1024 * 2];
    int peak = 0;
    for (long done = 0; done < frames;) {
        int n = frames - done < 1024 ? (int)(frames - done) : 1024;
        mixer_render(buf, n);
        for (int i = 0; i < 2 * n; i++) {
            int a = buf[i] < 0 ? -buf[i] : buf[i];
            if (a > peak)
                peak = a;
            put16(f, (unsigned)(uint16_t)buf[i]);
        }
        done += n;
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "vmus_render: write error\n");
        return 1;
    }
    uint32_t pass = music_order_start(song, song->norder);
    printf("vmus_render: %s -> %s, %d s, first pass %u.%02u s (loop to order %d), peak %d\n",
           argv[1], argv[2], seconds, pass / MIXER_RATE, pass % MIXER_RATE * 100 / MIXER_RATE,
           song->loop, peak);
    music_free(song);
    return 0;
}
