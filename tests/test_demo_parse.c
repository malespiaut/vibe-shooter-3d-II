/* demo_parse: the demo parser (tech.md Demos), playback and recorder. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/demo.h"

static const char *GOOD = "# a comment line\n"
                          "vsdemo 1\n"
                          "level 3   # trailing comment\n"
                          "skill 1\n"
                          "seed 42\n"
                          "map m1test\n"
                          "\n"
                          "wait 2\n"
                          "in 3 127 -127 -2048 FUR3\n"
                          "\tin 1 0 5 0 -\r\n";

static void expect_error(const char *text, const char *want)
{
    char err[256] = "";
    demo_t *d = demo_parse(text, err, sizeof err);
    if (d || !strstr(err, want)) {
        fprintf(stderr, "want error containing '%s', got '%s'\n", want, err);
        assert(0);
    }
}

static void test_good(void)
{
    char err[256] = "";
    demo_t *d = demo_parse(GOOD, err, sizeof err);
    if (!d)
        fprintf(stderr, "%s\n", err);
    assert(d);
    assert(d->level == 3 && d->skill == 1 && d->seed == 42);
    assert(strcmp(d->map, "m1test") == 0);
    assert(d->count == 3);
    assert(d->cmds[0].kind == DEMO_CMD_WAIT && d->cmds[0].n == 2 && d->cmds[0].line == 8);
    assert(d->cmds[1].kind == DEMO_CMD_IN && d->cmds[1].n == 3);
    assert(d->cmds[1].cmd.fwd == 127 && d->cmds[1].cmd.side == -127);
    assert(d->cmds[1].cmd.turn == -2048);
    assert(d->cmds[1].cmd.buttons == (BT_FIRE | BT_USE | BT_RUN | (3 << BT_WEAPON_SHIFT)));
    assert(d->cmds[2].cmd.side == 5 && d->cmds[2].cmd.buttons == 0);

    /* Playback: 2 + 3 + 1 tics, then the end. */
    static game_state gs;
    ticcmd c;
    int tics = 0;
    while (demo_next(d, &gs, &c, err, sizeof err) == DEMO_TIC) {
        if (tics < 2)
            assert(c.fwd == 0 && c.turn == 0 && c.buttons == 0);
        else if (tics < 5)
            assert(c.fwd == 127 && c.turn == -2048);
        else
            assert(c.side == 5);
        tics++;
    }
    assert(tics == 6);
    assert(demo_next(d, &gs, &c, err, sizeof err) == DEMO_END);
    demo_free(d);

    /* Defaults. */
    d = demo_parse("vsdemo 1\n", err, sizeof err);
    assert(d && d->level == 1 && d->skill == 2 && d->seed == 0 && d->map[0] == 0);
    assert(d->count == 0);
    demo_free(d);
}

static void test_errors(void)
{
    expect_error("vsdemo 1\nwait 1\njump 3\n", "line 3: unknown command 'jump'");
    expect_error("level 1\n", "line 1: expected 'vsdemo 1'");
    expect_error("vsdemo 2\n", "line 1: unsupported demo version");
    expect_error("", "empty demo");
    expect_error("vsdemo 1\nwait 1\nlevel 2\n", "line 3: header line 'level' after");
    expect_error("vsdemo 1\nskill 4\n", "line 2: skill: 4 out of range");
    expect_error("vsdemo 1\nin 1 128 0 0 -\n", "line 2: fwd: 128 out of range");
    expect_error("vsdemo 1\nin 1 0 0 40000 -\n", "line 2: turn");
    expect_error("vsdemo 1\nin 0 0 0 0 -\n", "line 2: tics");
    expect_error("vsdemo 1\nin 1 0 0 0\n", "line 2: 'in' takes 5 arguments, got 4");
    expect_error("vsdemo 1\n\n\nin 1 0 0 0 X\n", "line 4: buttons 'X': unknown button");
    expect_error("vsdemo 1\nin 1 0 0 0 12\n", "line 2: buttons '12': more than one weapon");
    expect_error("vsdemo 1\nwait abc\n", "line 2: tics: 'abc' is not an integer");
    expect_error("vsdemo 1\nmap ../etc\n", "line 2: map name");
    expect_error("vsdemo 1\nseed -1\n", "line 2: seed");
}

static void test_recorder(const char *path)
{
    demo_rec r;
    demo_rec_init(&r);
    const ticcmd a = { 127, 0, 0, BT_RUN }, b = { 0, -3, 1872, BT_FIRE | (7 << BT_WEAPON_SHIFT) };
    const ticcmd z = { 0, 0, 0, 0 };
    for (int i = 0; i < 5; i++)
        assert(demo_rec_add(&r, &a));
    assert(demo_rec_add(&r, &b));
    assert(demo_rec_add(&r, &z));
    assert(demo_rec_add(&r, &z));
    assert(r.len == 3);
    assert(demo_rec_write(&r, path, 2, 3, 0, "m1test"));
    demo_rec_free(&r);

    FILE *f = fopen(path, "rb");
    assert(f);
    static char text[4096];
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    const char *want = "vsdemo 1\nlevel 2\nskill 3\nseed 0\nmap m1test\n"
                       "in 5 127 0 0 R\nin 1 0 -3 1872 F7\nin 2 0 0 0 -\n";
    if (strcmp(text, want) != 0) {
        fprintf(stderr, "recorded:\n%s", text);
        assert(0);
    }
    /* What was recorded parses back to the same tics. */
    char err[256];
    demo_t *d = demo_parse(text, err, sizeof err);
    assert(d && d->count == 3 && d->level == 2 && d->skill == 3);
    assert(d->cmds[1].cmd.buttons == b.buttons && d->cmds[1].cmd.turn == 1872);
    demo_free(d);
}

int main(int argc, char **argv)
{
    test_good();
    test_errors();
    test_recorder(argc > 1 ? argv[1] : "demo_parse_rec.demo");
    printf("demo_parse: OK\n");
    return 0;
}
