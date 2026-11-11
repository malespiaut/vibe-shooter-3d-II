/* demo_parse: the demo parser (tech.md Demos), playback and recorder. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/demo.h"
#include "game/flow.h"

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
    assert(!d->on_death_restart);
    assert(d->count == 0);
    demo_free(d);
    d = demo_parse("vsdemo 1\non_death restart\n", err, sizeof err);
    assert(d && d->on_death_restart);
    demo_free(d);
    d = demo_parse("vsdemo 1\non_death end\n", err, sizeof err);
    assert(d && !d->on_death_restart);
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
    expect_error("vsdemo 1\non_death later\n", "line 2: on_death");
    expect_error("vsdemo 1\ngoto 3\n", "line 2: 'goto' takes");
    expect_error("vsdemo 1\ngoto 64 1\n", "line 2: tile x: 64 out of range");
    expect_error("vsdemo 1\ngoto @ 5\n", "line 2: goto: waypoint name");
    expect_error("vsdemo 1\ngoto @abcdefghijklmnopq\n", "line 2: goto: waypoint name");
    expect_error("vsdemo 1\ngoto 1 1 0\n", "line 2: max: 0 out of range");
    expect_error("vsdemo 1\nface\n", "line 2: 'face' takes");
    expect_error("vsdemo 1\nface 400\n", "line 2: angle: 400 out of range");
    expect_error("vsdemo 1\nuse 1\n", "line 2: 'use' takes 0 arguments");
    expect_error("vsdemo 1\nweapon 8\n", "line 2: weapon slot: 8 out of range");
    expect_error("vsdemo 1\nfire 0\n", "line 2: tics");
    expect_error("vsdemo 1\nexpect health\n", "line 2: expect 'health': want");
    expect_error("vsdemo 1\nexpect mana=3\n", "line 2: expect: unknown stat 'mana'");
    expect_error("vsdemo 1\nexpect health=>3\n", "line 2: value: '>3' is not");
    expect_error("vsdemo 1\nexpect health~3\n", "line 2: expect 'health~3': want");
    expect_error("vsdemo 1\nexpect tile<3,4\n", "line 2: expect tile: only = and !=");
    expect_error("vsdemo 1\nexpect tile=3\n", "line 2: expect tile: want X,Y");
    expect_error("vsdemo 1\nexpect tile=3,x\n", "line 2: tile y: 'x' is not");
    expect_error("vsdemo 1\nexpect health >=3\n", "line 2: 'expect' takes one");
}

/* High-level commands that need no map: parsing, then playback against a
 * zeroed state (tic 0, health 0, angle 0). */
static void test_high_level(void)
{
    char err[256] = "";
    demo_t *d = demo_parse("vsdemo 1\n"
                           "goto 5 7\n"
                           "goto @hall 40\n"
                           "face -90\n"
                           "face 3 4\n"
                           "use\n"
                           "weapon 3\n"
                           "fire 2\n"
                           "expect health>=0\n"
                           "expect tile!=1,2\n",
                           err, sizeof err);
    if (!d)
        fprintf(stderr, "%s\n", err);
    assert(d && d->count == 9);
    assert(d->cmds[0].kind == DEMO_CMD_GOTO && d->cmds[0].tx == 5 && d->cmds[0].ty == 7);
    assert(d->cmds[0].n == DEMO_GOTO_DEFAULT_MAX && d->cmds[0].name[0] == 0);
    assert(d->cmds[1].kind == DEMO_CMD_GOTO && strcmp(d->cmds[1].name, "hall") == 0);
    assert(d->cmds[1].n == 40);
    assert(d->cmds[2].kind == DEMO_CMD_FACE && d->cmds[2].angle == ANG270 && !d->cmds[2].has_tile);
    assert(d->cmds[3].kind == DEMO_CMD_FACE && d->cmds[3].has_tile && d->cmds[3].tx == 3);
    assert(d->cmds[4].kind == DEMO_CMD_IN && d->cmds[4].n == 1 && d->cmds[4].cmd.buttons == BT_USE);
    assert(d->cmds[5].cmd.buttons == (3 << BT_WEAPON_SHIFT) && d->cmds[5].n == 1);
    assert(d->cmds[6].cmd.buttons == BT_FIRE && d->cmds[6].n == 2);
    assert(d->cmds[7].kind == DEMO_CMD_EXPECT && d->cmds[7].op == DEMO_OP_GE);
    assert(d->cmds[8].op == DEMO_OP_NE && d->cmds[8].tx == 1 && d->cmds[8].ty == 2);
    demo_free(d);

    /* face 90 from angle 0: full-speed turns, then the remainder exactly. */
    d = demo_parse("vsdemo 1\nface 90\nuse\nweapon 2\nfire 2\nexpect tic=0\n"
                   "expect health<1\nexpect tile=0,0\n",
                   err, sizeof err);
    assert(d);
    static game_state gs;
    ticcmd c;
    int32_t turned = 0;
    int tics = 0;
    while (demo_next(d, &gs, &c, err, sizeof err) == DEMO_TIC && d->pc == 0) {
        assert(c.fwd == 0 && c.side == 0 && c.buttons == 0);
        assert(c.turn > 0 && c.turn <= PLAYER_TURN_SPEED);
        turned += c.turn;
        gs.player.angle = (angle_t)(gs.player.angle + c.turn);
        tics++;
    }
    assert(gs.player.angle == ANG90 && turned == ANG90);
    assert(tics == (ANG90 + PLAYER_TURN_SPEED - 1) / PLAYER_TURN_SPEED);
    assert(c.buttons == BT_USE); /* the use tic, already fetched */
    assert(demo_next(d, &gs, &c, err, sizeof err) == DEMO_TIC && c.buttons == (2 << BT_WEAPON_SHIFT));
    for (int i = 0; i < 2; i++)
        assert(demo_next(d, &gs, &c, err, sizeof err) == DEMO_TIC && c.buttons == BT_FIRE);
    /* The three expects hold against the zeroed state and use no tic. */
    assert(demo_next(d, &gs, &c, err, sizeof err) == DEMO_END);
    demo_free(d);

    /* A false expect fails, naming its line and the actual value. */
    d = demo_parse("vsdemo 1\nwait 1\n\nexpect health>=20\n", err, sizeof err);
    assert(d);
    gs.player.health = 7;
    assert(demo_next(d, &gs, &c, err, sizeof err) == DEMO_TIC);
    err[0] = 0;
    assert(demo_next(d, &gs, &c, err, sizeof err) == DEMO_FAIL);
    if (!strstr(err, "line 4: expect health>=20 failed: health is 7")) {
        fprintf(stderr, "got '%s'\n", err);
        assert(0);
    }
    demo_free(d);
}

/* Front-end flow lines (issue #101): `start`, `key`, `expect screen=`. */
static void test_flow_lines(void)
{
    char err[256] = "";
    demo_t *d = demo_parse("vsdemo 1\nwait 1\n", err, sizeof err);
    assert(d && d->start == FLOW_PLAY); /* default: start level */
    demo_free(d);
    d = demo_parse("vsdemo 1\nstart title\nkey any\nwait 1\nkey esc\nexpect screen=pause\n"
                   "expect screen!=title\n",
                   err, sizeof err);
    if (!d)
        fprintf(stderr, "%s\n", err);
    assert(d && d->start == FLOW_TITLE && d->count == 5);
    static game_state gs;
    ticcmd c;
    d->screen = FLOW_TITLE;
    assert(demo_next(d, &gs, &c, err, sizeof err) == DEMO_TIC && d->key == FKEY_ANY);
    assert(c.fwd == 0 && c.buttons == 0);
    assert(demo_next(d, &gs, &c, err, sizeof err) == DEMO_TIC && d->key == FKEY_NONE);
    assert(demo_next(d, &gs, &c, err, sizeof err) == DEMO_TIC && d->key == FKEY_ESC);
    d->screen = FLOW_PAUSE;
    assert(demo_next(d, &gs, &c, err, sizeof err) == DEMO_END);
    demo_free(d);
    d = demo_parse("vsdemo 1\nexpect screen=menu\n", err, sizeof err);
    d->screen = FLOW_TITLE;
    assert(demo_next(d, &gs, &c, err, sizeof err) == DEMO_FAIL);
    assert(strstr(err, "screen is title"));
    demo_free(d);
    expect_error("vsdemo 1\nstart menu\n", "start: want 'level' or 'title'");
    expect_error("vsdemo 1\nkey left\n", "key: 'left' is not one of");
    expect_error("vsdemo 1\nexpect screen=lobby\n", "unknown screen 'lobby'");
    expect_error("vsdemo 1\nexpect screen<title\n", "only = and != apply");
    expect_error("vsdemo 1\nwait 1\nstart title\n", "header line 'start' after");
    printf("ok flow lines: start, key, expect screen\n");
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
    test_high_level();
    test_flow_lines();
    test_recorder(argc > 1 ? argv[1] : "demo_parse_rec.demo");
    printf("demo_parse: OK\n");
    return 0;
}
