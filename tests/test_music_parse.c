/* ctest `music_parse`: .vmus parser. A good song parses with the expected
 * contents; malformed input fails with the offending line number.
 *
 *   test_music_parse <title.vmus> */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/music.h"

static char g_text[32768];

/* Builds a one-pattern song. Row `bad_row` (if >= 0) gets `bad_cell` in
 * channel 2. Returns the 1-based line number of pattern row 0. */
static int build(int bad_row, const char *bad_cell)
{
    char *p = g_text;
    p += sprintf(p, "# test song\nvmus 1\ntempo 5\n");
    p += sprintf(p, "inst 01 square vol=48 a=0 d=4 s=32 r=6 duty=64\n");
    p += sprintf(p, "inst 02 noise vol=40 d=3 s=0 mode=1\n");
    p += sprintf(p, "inst 1f pluck damp=250\n");
    p += sprintf(p, "\npattern 3\n");
    int first = 9;
    for (int r = 0; r < MUSIC_ROWS; r++) {
        const char *c2 = r == bad_row ? bad_cell : "--- .. .. ...";
        if (r == 0)
            p += sprintf(p, "C-4 01 40 A37 | C#2 02 .. U10 | %s | B-8 1f 20 V48\n", c2);
        else if (r == 1)
            p += sprintf(p, "=== .. .. ... | --- .. 10 D02 | %s | --- .. .. ...  # comment\n", c2);
        else
            p += sprintf(p, "--- .. .. ... | --- .. .. ... | %s | --- .. .. ...\n", c2);
    }
    p += sprintf(p, "order 3 3\nloop 1\n");
    return first;
}

static void expect_fail(const char *text, int line)
{
    char err[200], want[32];
    music_song *s = music_parse(text, err, sizeof err);
    assert(s == NULL);
    snprintf(want, sizeof want, "line %d:", line);
    if (strncmp(err, want, strlen(want)) != 0) {
        fprintf(stderr, "expected '%s...', got '%s'\n", want, err);
        assert(0);
    }
    printf("ok: %s\n", err);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    char err[200];

    int row0 = build(-1, NULL);
    music_song *s = music_parse(g_text, err, sizeof err);
    if (!s)
        fprintf(stderr, "%s\n", err);
    assert(s);
    assert(s->tempo == 5);
    assert(s->norder == 2 && s->order[0] == 3 && s->order[1] == 3 && s->loop == 1);
    assert(s->pat_defined[3] && !s->pat_defined[0]);
    assert(s->inst[1].defined && s->inst[1].wave == MUSIC_WAVE_SQUARE);
    assert(s->inst[1].vol == 48 && s->inst[1].d == 4 && s->inst[1].s == 32);
    assert(s->inst[1].r == 6 && s->inst[1].duty == 64);
    assert(s->inst[2].wave == MUSIC_WAVE_NOISE && s->inst[2].mode == 1 && s->inst[2].s == 0);
    assert(s->inst[31].wave == MUSIC_WAVE_PLUCK && s->inst[31].damp == 250);
    assert(s->inst[31].vol == 64 && s->inst[31].s == 64); /* defaults */
    const music_cell *c = s->pat[3][0];
    assert(c[0].note == 60 && c[0].inst == 1 && c[0].vol == 64);
    assert(c[0].fx == 'A' && c[0].fxp == 0x37);
    assert(c[1].note == 37 && c[1].inst == 2 && c[1].vol == MUSIC_VOL_NONE);
    assert(c[1].fx == 'U' && c[1].fxp == 0x10);
    assert(c[2].note == MUSIC_NOTE_NONE && c[2].inst == 0 && c[2].fx == 0);
    assert(c[3].note == 119 && c[3].inst == 31 && c[3].vol == 32 && c[3].fx == 'V');
    c = s->pat[3][1];
    assert(c[0].note == MUSIC_NOTE_OFF && c[1].vol == 16 && c[1].fx == 'D');
    assert(music_order_start(s, 1) == 64u * 5 * MUSIC_TIC_SAMPLES);
    music_free(s);
    printf("ok: good song parses\n");

    /* Malformed rows: the error names the row's own line. */
    build(10, "C-4 01 40 A3");
    expect_fail(g_text, row0 + 10);
    build(20, "H-4 01 40 ...");
    expect_fail(g_text, row0 + 20);
    build(30, "C-4 07 40 ...");  /* undefined instrument */
    expect_fail(g_text, row0 + 30);
    build(40, "C-4 01 41 ...");  /* volume past 0x40 */
    expect_fail(g_text, row0 + 40);
    build(50, "C-4 01 40 ... X"); /* extra field */
    expect_fail(g_text, row0 + 50);
    build(63, "C-4 01 40 Z00");
    expect_fail(g_text, row0 + 63);

    /* Structural errors. */
    expect_fail("vmus 2\n", 1);
    expect_fail("vmus 1\ntempo 0\n", 2);
    expect_fail("vmus 1\ntempo 4\ninst 01 sine\n", 3);
    expect_fail("vmus 1\ntempo 4\ninst 01 square duty=300\n", 3);
    expect_fail("vmus 1\ntempo 4\nbogus 1\n", 3);
    expect_fail("vmus 1\ntempo 4\n\npattern 0\n--- .. .. ... | --- .. .. ... | --- .. .. ... | --- .. .. ...\n", 4);

    /* More than MAX_TOK (129) tokens on one line fails cleanly, not an
     * out-of-bounds read of tok[] (reviewer repro: 140 order entries). */
    {
        static char big[1024];
        char *q = big + sprintf(big, "vmus 1\ntempo 4\norder");
        for (int i = 0; i < 140; i++)
            q += sprintf(q, " 0");
        sprintf(q, "\n");
        expect_fail(big, 3);
        char err2[200];
        assert(!music_parse(big, err2, sizeof err2) && strstr(err2, "too many tokens"));
        q = big + sprintf(big, "vmus 1\ntempo 4\n\ninst 01 square");
        for (int i = 0; i < 130; i++)
            q += sprintf(q, " x");
        sprintf(q, "\n");
        expect_fail(big, 4);
        /* 128 entries on one line is the maximum; more across order lines
         * (order entries accumulate) hits 'bad order entry'. */
        q = big + sprintf(big, "vmus 1\ntempo 4\norder");
        for (int i = 0; i < 128; i++)
            q += sprintf(q, " 0");
        q += sprintf(q, "\norder 0\n");
        expect_fail(big, 4);
        assert(!music_parse(big, err2, sizeof err2) && strstr(err2, "bad order entry"));
    }
    build(-1, NULL);
    strcpy(strstr(g_text, "loop 1"), "loop 2\n");
    expect_fail(g_text, row0 + 65);
    build(-1, NULL);
    strcpy(strstr(g_text, "order 3 3"), "order 3 4\nloop 0\n");
    expect_fail(g_text, row0 + 64);

    /* The shipped title track parses. */
    s = music_load(argv[1], err, sizeof err);
    if (!s)
        fprintf(stderr, "%s\n", err);
    assert(s);
    printf("ok: %s: tempo %d, %d orders, loop %d\n", argv[1], s->tempo, s->norder, s->loop);
    music_free(s);
    return 0;
}
