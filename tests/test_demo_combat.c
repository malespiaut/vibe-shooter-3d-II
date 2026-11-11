/* Demo combat helpers (tech.md Demos: face nearest, fire nearest, clear;
 * issue #98). Maps are built in memory with libbast, as test_ai.
 * Usage: test_demo_combat <group>. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "game/demo.h"
#include "game/demo_combat.h"
#include "game/demo_nav.h"
#include "game/state.h"

#define FX(i) fix_from_int(i)
#define CENTRE(i) (FX(i) + FIX_ONE / 2)

static bast_map *open_map(int w, int h)
{
    bast_map *m = bast_map_new(w, h);
    assert(m);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1)
                bast_map_cell_at(m, x, y)->flags = BAST_CELL_SOLID;
    return m;
}

static void add_entity(bast_map *m, uint16_t type, int tx, int ty, angle_t angle)
{
    int i = bast_map_add_entity(m);
    assert(i >= 0);
    m->entities[i].type = type;
    m->entities[i].x = CENTRE(tx);
    m->entities[i].y = CENTRE(ty);
    m->entities[i].angle = angle;
    m->entities[i].flags = BAST_ENT_SKILL_ALL;
}

/* Plays `body` (after a header) against gs until the demo ends; returns the
 * last demo_next status. */
static int play(game_state *gs, const char *body, char *err, size_t errlen)
{
    char text[512];
    snprintf(text, sizeof text, "vsdemo 1\n%s", body);
    demo_t *d = demo_parse(text, err, errlen);
    assert(d);
    int s;
    ticcmd cmd;
    while ((s = demo_next(d, gs, &cmd, err, errlen)) == DEMO_TIC)
        game_tic(gs, &cmd);
    demo_free(d);
    return s;
}

/* Player at (2, 4) facing north. Mutts (ids in entity order):
 *  1: (9, 4), 7 units east, visible
 *  2: (2, 1), 3 units south, behind the wall row y = 2: nearest, hidden
 *  3: (6, 7), sqrt(16 + 9) = 5 units north-east, visible: the one to pick
 * All face away (east), so none wakes on sight while the player turns. */
static void test_face(void)
{
    bast_map *m = open_map(14, 10);
    for (int x = 1; x <= 5; x++)
        bast_map_cell_at(m, x, 2)->flags = BAST_CELL_SOLID;
    add_entity(m, BAST_ENT_PLAYER_START, 2, 4, ANG90);
    add_entity(m, BAST_ENT_MUTT_TROOPER, 9, 4, 0);
    add_entity(m, BAST_ENT_MUTT_TROOPER, 2, 1, 0);
    add_entity(m, BAST_ENT_MUTT_TROOPER, 6, 7, 0);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    assert(gs.things.count == 3);

    assert(combat_visible(&gs, 1, 0));
    assert(!combat_visible(&gs, 2, 0)); /* wall */
    assert(combat_visible(&gs, 3, 0));
    assert(!combat_visible(&gs, 3, FX(4))); /* 5 units: outside a 4-unit radius */
    assert(!combat_visible(&gs, 0, 0) && !combat_visible(&gs, 4, 0));
    assert(combat_nearest(&gs, 0) == 3);
    assert(combat_nearest(&gs, FX(4)) == 0);

    char err[256] = "";
    int s = play(&gs, "face nearest\n", err, sizeof err);
    assert(s == DEMO_END);
    const angle_t want = angle_of(CENTRE(6) - gs.player.x, CENTRE(7) - gs.player.y);
    int32_t e = (int16_t)(uint16_t)(want - gs.player.angle);
    printf("face: picked 3 after %u tics, angle %u, error %d\n", (unsigned)gs.tic,
           (unsigned)gs.player.angle, (int)e);
    assert(e >= -NAV_FACE_TOL && e <= NAV_FACE_TOL);
    /* From north to atan2(3, 4) = 36.9 degrees: 53.1 degrees (9671 units)
     * clockwise at 1872 per tic is 6 tics. */
    assert(gs.tic == 6);
    /* Turning in place moved nothing and woke nobody. */
    assert(gs.player.x == CENTRE(2) && gs.player.y == CENTRE(4));
    for (int i = 0; i < 3; i++)
        assert(gs.things.list[i].state == TH_STATE_STAND);

    /* Kill 3: the nearest visible is now 1, the hidden 2 is still ignored. */
    gs.things.list[2].state = TH_STATE_DEAD;
    assert(combat_nearest(&gs, 0) == 1);
    gs.things.list[0].state = TH_STATE_DEAD;
    assert(combat_nearest(&gs, 0) == 0);
    s = play(&gs, "face nearest\n", err, sizeof err);
    assert(s == DEMO_FAIL);
    printf("face: %s\n", err);
    assert(strstr(err, "line 2: face nearest: no visible enemy at tic"));
    bast_map_free(m);
}

/* Two Mutts at the same distance (east and west): the lower id wins. */
static void test_tie(void)
{
    bast_map *m = open_map(14, 6);
    add_entity(m, BAST_ENT_PLAYER_START, 6, 3, ANG90);
    add_entity(m, BAST_ENT_MUTT_TROOPER, 10, 3, 0);
    add_entity(m, BAST_ENT_MUTT_TROOPER, 2, 3, 0);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    assert(combat_nearest(&gs, 0) == 1);
    /* Swap the order: still the lower id, now the west one. */
    thinker_t t = gs.things.list[0];
    gs.things.list[0] = gs.things.list[1];
    gs.things.list[1] = t;
    assert(combat_nearest(&gs, 0) == 1);
    printf("tie: lower id wins\n");
    bast_map_free(m);
}

/* Aim: turn capped at PLAYER_TURN_SPEED, residual error reported; strafe
 * pattern 17 left then 18 right. */
static void test_aim(void)
{
    bast_map *m = open_map(14, 6);
    add_entity(m, BAST_ENT_PLAYER_START, 2, 3, ANG90);
    add_entity(m, BAST_ENT_MUTT_TROOPER, 10, 3, 0);
    game_state gs;
    game_init(&gs, m, 1, 2, 0);
    ticcmd cmd;
    int32_t left = combat_aim(&gs, 1, &cmd);
    assert(cmd.turn == -PLAYER_TURN_SPEED && cmd.fwd == 0 && cmd.side == 0 && !cmd.buttons);
    assert(left == ANG90 - PLAYER_TURN_SPEED);
    gs.player.angle = 100;
    left = combat_aim(&gs, 1, &cmd);
    assert(cmd.turn == -100 && left == 0);
    for (int t = 0; t < 70; t++)
        assert(combat_strafe(t) == ((t % 35) < 17 ? -TICCMD_MAX : TICCMD_MAX));
    printf("aim: ok\n");
    bast_map_free(m);
}

/* Parse errors are clear. */
static void test_parse(void)
{
    static const char *const bad[] = {
        "vsdemo 1\nfire nearest 0\n",     "vsdemo 1\nfire nearest 5 6\n",
        "vsdemo 1\nclear 0\n",            "vsdemo 1\nclear 65\n",
        "vsdemo 1\nclear 10 0\n",         "vsdemo 1\nclear 1 2 3\n",
        "vsdemo 1\nface nearest 3\n",
    };
    char err[256];
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        demo_t *d = demo_parse(bad[i], err, sizeof err);
        assert(!d);
        printf("parse: %s\n", err);
        assert(strncmp(err, "line 2:", 7) == 0);
    }
    demo_t *d = demo_parse("vsdemo 1\nface nearest\nfire nearest\nfire nearest 99\nclear\n"
                           "clear 5\nclear 6 700\n",
                           err, sizeof err);
    assert(d && d->count == 6);
    assert(d->cmds[0].kind == DEMO_CMD_FACE_NEAREST);
    assert(d->cmds[1].kind == DEMO_CMD_FIRE_NEAREST && d->cmds[1].n == COMBAT_FIRE_DEFAULT_MAX);
    assert(d->cmds[2].n == 99);
    assert(d->cmds[3].kind == DEMO_CMD_CLEAR && d->cmds[3].value == 10 && d->cmds[3].n == 2100);
    assert(d->cmds[4].value == 5 && d->cmds[4].n == 2100);
    assert(d->cmds[5].value == 6 && d->cmds[5].n == 700);
    demo_free(d);
}

int main(int argc, char **argv)
{
    const char *g = argc > 1 ? argv[1] : "";
    if (!strcmp(g, "face"))
        test_face();
    else if (!strcmp(g, "tie"))
        test_tie();
    else if (!strcmp(g, "aim"))
        test_aim();
    else if (!strcmp(g, "parse"))
        test_parse();
    else {
        fprintf(stderr, "usage: test_demo_combat face|tie|aim|parse\n");
        return 2;
    }
    return 0;
}
