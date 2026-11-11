/* ctest flow_*: the front-end screen state machine (game/flow.h, hud.md Flow).
 *
 *   test_flow <maps/m1test.vmap> <blink|wrap|pause_freeze|restart|quit_title|skill>
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "game/flow.h"
#include "game/state.h"

static game_state gs, entry;
static const ticcmd IDLE = { 0, 0, 0, 0 };
static const ticcmd WALK = { 127, 0, 0, 0 };

static flow_world world(const bast_map *map)
{
    flow_world w = { &gs, &entry, map, 1, 0 };
    return w;
}

/* title -> menu -> skill (Tomcat) -> play, as flow.demo does. */
static void start_game(flow_t *f, flow_world *w)
{
    flow_init(f, FLOW_TITLE, w);
    flow_tic(f, FKEY_ANY, w, &IDLE);
    assert(f->screen == FLOW_MENU && f->cursor == MENU_NEW_GAME);
    flow_tic(f, FKEY_ENTER, w, &IDLE);
    assert(f->screen == FLOW_SKILL && f->cursor == FLOW_DEFAULT_SKILL - 1);
    int r = flow_tic(f, FKEY_ENTER, w, &IDLE);
    assert(f->screen == FLOW_PLAY);
    assert(!(r & FLOW_TICKED)); /* the selecting tic is not a game tic */
    assert(gs.tic == 0 && gs.skill == FLOW_DEFAULT_SKILL);
}

static void test_blink(void)
{
    flow_t f;
    flow_world w = world(NULL);
    flow_init(&f, FLOW_TITLE, &w);
    /* On for 35 tics, off for 35: period 70. */
    int on[140];
    for (int t = 0; t < 140; t++) {
        on[t] = flow_title_blink_on(&f);
        assert(!(flow_tic(&f, FKEY_NONE, &w, &IDLE) & FLOW_TICKED));
        assert(f.screen == FLOW_TITLE);
    }
    for (int t = 0; t < 140; t++) {
        assert(on[t] == (t % 70 < 35));
        assert(on[t] == on[(t + 70) % 140]);
    }
    printf("ok blink period 70 (35 on, 35 off)\n");
}

static void test_wrap(void)
{
    flow_t f;
    flow_world w = world(NULL);
    flow_init(&f, FLOW_TITLE, &w);
    flow_tic(&f, FKEY_ANY, &w, &IDLE);
    assert(f.screen == FLOW_MENU && f.cursor == MENU_NEW_GAME);
    flow_tic(&f, FKEY_DOWN, &w, &IDLE);
    assert(f.cursor == MENU_CONTROLS && f.sound == FSND_MOVE);
    flow_tic(&f, FKEY_DOWN, &w, &IDLE);
    assert(f.cursor == MENU_QUIT);
    flow_tic(&f, FKEY_DOWN, &w, &IDLE);
    assert(f.cursor == MENU_NEW_GAME); /* down wraps from Quit to New Game */
    flow_tic(&f, FKEY_UP, &w, &IDLE);
    assert(f.cursor == MENU_QUIT); /* and up wraps back */
    assert(flow_tic(&f, FKEY_ENTER, &w, &IDLE) & FLOW_EXIT);
    printf("ok down wraps Quit -> New Game, up wraps back, Quit exits\n");
}

static void test_pause_freeze(const bast_map *map)
{
    flow_t f;
    flow_world w = world(map);
    start_game(&f, &w);
    for (int i = 0; i < 20; i++)
        assert(flow_tic(&f, FKEY_NONE, &w, &WALK) & FLOW_TICKED);
    assert(gs.tic == 20);
    uint64_t h = game_hash(&gs);
    assert(!(flow_tic(&f, FKEY_ESC, &w, &WALK) & FLOW_TICKED));
    assert(f.screen == FLOW_PAUSE && f.cursor == PAUSE_RESUME);
    for (int i = 0; i < 50; i++) {
        assert(!(flow_tic(&f, i & 1 ? FKEY_NONE : FKEY_DOWN, &w, &WALK) & FLOW_TICKED));
        assert(game_hash(&gs) == h && gs.tic == 20);
    }
    /* Esc in pause resumes too. */
    flow_tic(&f, FKEY_ESC, &w, &IDLE);
    assert(f.screen == FLOW_PLAY && game_hash(&gs) == h);
    assert(flow_tic(&f, FKEY_NONE, &w, &WALK) & FLOW_TICKED);
    assert(gs.tic == 21 && game_hash(&gs) != h);
    printf("ok Esc pauses, hash frozen at tic 20 for 50 tics, resumes at 21\n");
}

static void test_restart(const bast_map *map)
{
    flow_t f;
    flow_world w = world(map);
    start_game(&f, &w);
    uint64_t h0 = game_hash(&gs);
    for (int i = 0; i < 40; i++)
        flow_tic(&f, FKEY_NONE, &w, &WALK);
    assert(game_hash(&gs) != h0);
    flow_tic(&f, FKEY_ESC, &w, &IDLE);
    flow_tic(&f, FKEY_DOWN, &w, &IDLE);
    assert(f.cursor == PAUSE_RESTART);
    flow_tic(&f, FKEY_ENTER, &w, &IDLE);
    assert(f.screen == FLOW_PLAY);
    assert(gs.tic == 0 && game_hash(&gs) == h0); /* level entry state */
    printf("ok Restart Level restores the entry state (hash %016llx)\n",
           (unsigned long long)h0);
}

static void test_quit_title(const bast_map *map)
{
    flow_t f;
    flow_world w = world(map);
    start_game(&f, &w);
    flow_tic(&f, FKEY_NONE, &w, &WALK);
    flow_tic(&f, FKEY_ESC, &w, &IDLE);
    for (int i = 0; i < PAUSE_QUIT_TITLE; i++)
        flow_tic(&f, FKEY_DOWN, &w, &IDLE);
    assert(f.cursor == PAUSE_QUIT_TITLE);
    int r = flow_tic(&f, FKEY_ENTER, &w, &IDLE);
    assert(f.screen == FLOW_TITLE && !(r & (FLOW_TICKED | FLOW_EXIT)));
    assert(f.timer == 0 && flow_title_blink_on(&f));
    printf("ok Quit to Title returns to TITLE\n");
}

static void test_skill(const bast_map *map)
{
    flow_t f;
    flow_world w = world(map);
    flow_init(&f, FLOW_TITLE, &w);
    flow_tic(&f, FKEY_ENTER, &w, &IDLE); /* any key, Enter included */
    assert(f.screen == FLOW_MENU);
    flow_tic(&f, FKEY_ENTER, &w, &IDLE);
    assert(f.screen == FLOW_SKILL);
    flow_tic(&f, FKEY_ESC, &w, &IDLE);
    assert(f.screen == FLOW_MENU && f.cursor == MENU_NEW_GAME);
    flow_tic(&f, FKEY_ENTER, &w, &IDLE);
    flow_tic(&f, FKEY_DOWN, &w, &IDLE);
    flow_tic(&f, FKEY_ENTER, &w, &IDLE);
    assert(f.screen == FLOW_PLAY && gs.skill == 3);
    /* Controls from pause returns to pause. */
    flow_tic(&f, FKEY_ESC, &w, &IDLE);
    flow_tic(&f, FKEY_UP, &w, &IDLE);
    flow_tic(&f, FKEY_UP, &w, &IDLE);
    assert(f.cursor == PAUSE_CONTROLS);
    flow_tic(&f, FKEY_ENTER, &w, &IDLE);
    assert(f.screen == FLOW_CONTROLS);
    flow_tic(&f, FKEY_ESC, &w, &IDLE);
    assert(f.screen == FLOW_PAUSE && f.cursor == PAUSE_CONTROLS);
    assert(flow_screen_from_name("pause") == FLOW_PAUSE);
    assert(strcmp(flow_screen_name(FLOW_SKILL), "skill") == 0);
    printf("ok skill Sabretooth starts at skill 3, controls returns to pause\n");
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: test_flow <m1test.vmap> <case>\n");
        return 1;
    }
    bast_map *map = bast_map_load(argv[1]);
    assert(map);
    const char *c = argv[2];
    if (strcmp(c, "blink") == 0)
        test_blink();
    else if (strcmp(c, "wrap") == 0)
        test_wrap();
    else if (strcmp(c, "pause_freeze") == 0)
        test_pause_freeze(map);
    else if (strcmp(c, "restart") == 0)
        test_restart(map);
    else if (strcmp(c, "quit_title") == 0)
        test_quit_title(map);
    else if (strcmp(c, "skill") == 0)
        test_skill(map);
    else {
        fprintf(stderr, "unknown case %s\n", c);
        return 1;
    }
    bast_map_free(map);
    return 0;
}
