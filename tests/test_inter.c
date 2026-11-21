/* ctest inter_*: between-level screens (issue #102, hud.md Flow, story.md):
 * the intermission tally, the story text pages, the carried inventory, the
 * level change and the credits scroll.
 *
 *   test_inter <maps dir> <percent|time|tally_count|carry|progress|ending|credits|wrap>
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "game/flow.h"
#include "game/state.h"
#include "game/story_text.h"

static game_state gs, entry;
static const ticcmd IDLE = { 0, 0, 0, 0 };
static char maps_dir[512];
static bast_map *loaded;
static int load_calls, load_level_asked;

static bast_map *load(const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.vmap", maps_dir, name);
    bast_map *m = bast_map_load(path);
    assert(m);
    return m;
}

static const bast_map *loader(void *ctx, int level)
{
    (void)ctx;
    load_calls++;
    load_level_asked = level;
    char name[32];
    snprintf(name, sizeof name, "level%d", level);
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.vmap", maps_dir, name);
    bast_map *m = bast_map_load(path);
    if (!m)
        return NULL;
    bast_map_free(loaded); /* the test keeps one; the old one is unused by now */
    loaded = m;
    return m;
}

static void test_percent(void)
{
    assert(tally_percent(7, 9) == 77); /* 77.7 rounded down */
    assert(tally_percent(0, 0) == 100);
    assert(tally_percent(9, 9) == 100);
    assert(tally_percent(0, 9) == 0);
    assert(tally_percent(2, 3) == 66);
    printf("ok tally 7/9 = 77%%, 0/0 = 100%%, 2/3 = 66%%\n");
}

static void test_time(void)
{
    char b[16];
    tally_format_time(252, b, sizeof b);
    assert(strcmp(b, "4:12") == 0);
    tally_format_time(0, b, sizeof b);
    assert(strcmp(b, "0:00") == 0);
    tally_format_time(605, b, sizeof b);
    assert(strcmp(b, "10:05") == 0);
    printf("ok time 252 s = 4:12, 0 = 0:00, 605 = 10:05\n");
}

/* Counting: one value step per tic, lines in order, then done. */
static void test_tally_count(void)
{
    bast_map *map = load("tagstest");
    game_init(&gs, map, 1, 2, 0);
    gs.things.kills = 7;
    gs.things.kill_total = 9;
    gs.items_got = 1;
    gs.items_total = 2;
    gs.secrets = 1;
    gs.tic = 252 * TICRATE + 34; /* 4:12, the partial second dropped */
    tally_t t;
    tally_init(&t, &gs);
    assert(t.target[TALLY_KILLS] == 77 && t.target[TALLY_ITEMS] == 50);
    assert(t.target[TALLY_SECRETS] == 1 && t.secret_total == gs.secret_total);
    assert(t.target[TALLY_TIME] == 252 && t.par == map->par_seconds);
    assert(strcmp(t.name, "TAGS TEST") == 0);
    int steps = 0;
    while (tally_step(&t)) {
        steps++;
        if (steps == 77)
            assert(t.shown[TALLY_KILLS] == 77 && t.shown[TALLY_ITEMS] == 0);
        if (steps == 78)
            assert(t.shown[TALLY_ITEMS] == 1);
    }
    assert(steps == 77 + 50 + 1 + 252 && t.done);
    printf("ok tally counts 1 per tic: %d tics for 77%% 50%% 1 4:12\n", steps);
    bast_map_free(map);
}

static void test_carry(void)
{
    bast_map *map = load("tagstest");
    game_init(&gs, map, 1, 2, 0);
    player_t *p = &gs.player;
    p->health = 142;
    p->armour = 60;
    p->armour_class = ARMOUR_PLATE;
    p->satchel = 1;
    p->weapons.owned |= 1u << 3;
    p->weapons.ammo[0] = 77;
    game_give_keys(&gs, KEY_GREEN | KEY_BLUE | KEY_VIOLET);
    p->catnip_tics = 300;
    p->catseye_tics = 400;
    p->ninth_life = 1;
    p->invuln_tics = 20;
    const uint32_t owned = p->weapons.owned;
    inventory_t inv;
    game_carry(&gs, &inv);
    bast_map *next = load("level2");
    game_init_with(&gs, next, 2, 2, 0, &inv);
    assert(p->health == 142 && p->armour == 60 && p->armour_class == ARMOUR_PLATE);
    assert(p->satchel == 1 && p->weapons.owned == owned && p->weapons.ammo[0] == 77);
    assert(p->keys == 0);
    assert(p->catnip_tics == 0 && p->catseye_tics == 0 && p->ninth_life == 0 &&
           p->invuln_tics == 0);
    /* The new level's entry snapshot is the carried inventory (death restart). */
    assert(gs.entry.health == 142 && gs.entry.ammo[0] == 77);
    printf("ok carry keeps health 142, armour 60, weapons, ammo; drops keys and powerups\n");
    bast_map_free(map);
    bast_map_free(next);
}

/* title -> skill -> intro -> play on `map` at `level`. */
static void start(flow_t *f, flow_world *w)
{
    flow_init(f, FLOW_TITLE, w);
    flow_tic(f, FKEY_ANY, w, &IDLE);
    flow_tic(f, FKEY_ENTER, w, &IDLE);
    flow_tic(f, FKEY_ENTER, w, &IDLE);
    assert(f->screen == FLOW_INTRO);
    while (f->screen == FLOW_INTRO)
        flow_tic(f, FKEY_ANY, w, &IDLE);
    assert(f->screen == FLOW_PLAY);
}

/* Exit -> tally (any key skips, second continues) -> story -> level 2 with
 * the carried inventory. */
static void test_progress(void)
{
    bast_map *map = load("tagstest");
    flow_world w = { &gs, &entry, map, 1, 0, loader, NULL };
    flow_t f;
    start(&f, &w);
    for (int i = 0; i < 10; i++)
        flow_tic(&f, FKEY_NONE, &w, &IDLE);
    gs.player.health = 64;
    game_give_keys(&gs, KEY_GREEN);
    gs.player.catnip_tics = 500;
    gs.exit = 1; /* as the EXIT cell sets it in the next game tic */
    int r = flow_tic(&f, FKEY_NONE, &w, &IDLE);
    assert((r & FLOW_TICKED) && (r & FLOW_LEVELDONE) && f.screen == FLOW_TALLY);
    assert(f.done_level == 1 && !f.tally.done);
    flow_tic(&f, FKEY_NONE, &w, &IDLE);
    flow_tic(&f, FKEY_NONE, &w, &IDLE);
    assert(f.tally.shown[TALLY_KILLS] == 2); /* 1 per tic */
    flow_tic(&f, FKEY_ANY, &w, &IDLE); /* skip the counting */
    assert(f.screen == FLOW_TALLY && f.tally.done && f.sound == FSND_TALLY_DONE);
    assert(f.tally.shown[TALLY_KILLS] == 100); /* 0/0 */
    assert(f.tally.shown[TALLY_TIME] == f.tally.target[TALLY_TIME]);
    flow_tic(&f, FKEY_ANY, &w, &IDLE); /* continue */
    assert(f.screen == FLOW_STORY && f.page == 0);
    char text[1024];
    assert(flow_page_text(&f, text, sizeof text));
    assert(strncmp(text, "THE GATE IS OPEN.", 17) == 0);
    assert(load_calls == 0);
    r = flow_tic(&f, FKEY_ANY, &w, &IDLE);
    assert(load_calls == 1 && load_level_asked == 2);
    assert(f.screen == FLOW_PLAY && (r & FLOW_NEWLEVEL) && !(r & FLOW_TICKED));
    assert(w.level == 2 && w.map == loaded && gs.map == loaded && gs.level == 2);
    assert(gs.tic == 0 && gs.player.health == 64 && gs.player.keys == 0);
    assert(gs.player.catnip_tics == 0 && gs.skill == FLOW_DEFAULT_SKILL);
    assert(game_hash(&entry) == game_hash(&gs)); /* Restart Level target */
    printf("ok exit -> tally -> story -> level 2, health 64 kept, keys and catnip dropped\n");
    bast_map_free(map);
}

/* Level 8's tally leads to the ending (2 screens) and the credits. */
static void test_ending(void)
{
    bast_map *map = load("tagstest");
    flow_world w = { &gs, &entry, map, FLOW_LAST_LEVEL, 0, loader, NULL };
    flow_t f;
    start(&f, &w);
    gs.exit = 1;
    flow_tic(&f, FKEY_NONE, &w, &IDLE);
    assert(f.screen == FLOW_TALLY && f.done_level == FLOW_LAST_LEVEL);
    while (!f.tally.done)
        flow_tic(&f, FKEY_NONE, &w, &IDLE);
    flow_tic(&f, FKEY_ENTER, &w, &IDLE);
    assert(f.screen == FLOW_ENDING && f.page == 0);
    char text[1024];
    assert(flow_page_text(&f, text, sizeof text) && strncmp(text, "WOOFU'S", 7) == 0);
    flow_tic(&f, FKEY_ANY, &w, &IDLE);
    assert(f.screen == FLOW_ENDING && f.page == 1);
    assert(flow_page_text(&f, text, sizeof text) && strstr(text, "END OF EPISODE ONE"));
    flow_tic(&f, FKEY_ANY, &w, &IDLE);
    assert(f.screen == FLOW_CREDITS && load_calls == 0);
    printf("ok level 8 tally -> ending (2 screens) -> credits, no level load\n");
    bast_map_free(map);
}

static void test_credits(void)
{
    flow_world w = { &gs, &entry, NULL, 1, 0, NULL, NULL };
    flow_t f;
    flow_init(&f, FLOW_ENDING, &w);
    flow_tic(&f, FKEY_ANY, &w, &IDLE);
    flow_tic(&f, FKEY_ANY, &w, &IDLE);
    assert(f.screen == FLOW_CREDITS);
    const int h = story_credits_lines * FLOW_CREDITS_LINE_H;
    assert(flow_credits_height() == h);
    assert(flow_credits_tics() == (uint32_t)(2 * (h + 240)));
    uint32_t tics = 0;
    while (f.screen == FLOW_CREDITS) {
        flow_tic(&f, FKEY_NONE, &w, &IDLE);
        tics++;
        assert(tics <= 100000);
    }
    assert(f.screen == FLOW_TITLE);
    assert(tics == (uint32_t)(2 * (h + 240)));
    printf("ok credits: text height %d px, title after exactly %u tics = 2 * (%d + 240)\n", h,
           tics, h);
}

/* Every story page: at most 30 characters (300 px in the 10 px menu font)
 * per line and 12 lines; intro and ending fit one page per screen. */
static void check_pages(const char *src, int expect_pages)
{
    int n = story_page_count(src);
    assert(n >= 1);
    if (expect_pages)
        assert(n == expect_pages);
    for (int p = 0; p < n; p++) {
        char buf[2048];
        assert(story_page(src, p, buf, sizeof buf));
        int lines = 1, col = 0, widest = 0;
        for (const char *c = buf; *c; c++) {
            if (*c == '\n') {
                lines++;
                col = 0;
            } else if (++col > widest) {
                widest = col;
            }
        }
        assert(widest * 10 <= 300);
        assert(lines <= STORY_LINES);
        assert(buf[0] != '\n'); /* no blank line at a page top */
    }
    assert(!story_page(src, n, (char[8]){ 0 }, 8));
}

static void test_wrap(void)
{
    for (int i = 0; i < STORY_INTRO_SCREENS; i++)
        check_pages(story_intro[i], 1);
    for (int i = 0; i < STORY_ENDING_SCREENS; i++)
        check_pages(story_ending[i], 1);
    for (int i = 0; i < STORY_INTERMISSIONS; i++)
        check_pages(story_intermission[i], 1);
    /* Overflow continues on a new page. */
    static char longtext[4096];
    longtext[0] = '\0';
    for (int i = 0; i < 40; i++)
        strcat(longtext, "MAU HAS NOT HAD BREAKFAST. ");
    check_pages(longtext, 0);
    assert(story_page_count(longtext) >= 2);
    /* Soft line breaks join; the indent of END OF EPISODE ONE stays. */
    char out[256];
    int n = story_wrap("AB\nCD\n\n   EF", 30, out, sizeof out);
    assert(n == 3 && strcmp(out, "AB CD\n\n   EF") == 0);
    printf("ok every story page <= 30 chars (300 px) x 12 lines, overflow paginates\n");
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: test_inter <maps dir> <case>\n");
        return 1;
    }
    snprintf(maps_dir, sizeof maps_dir, "%s", argv[1]);
    const char *c = argv[2];
    if (!strcmp(c, "percent"))
        test_percent();
    else if (!strcmp(c, "time"))
        test_time();
    else if (!strcmp(c, "tally_count"))
        test_tally_count();
    else if (!strcmp(c, "carry"))
        test_carry();
    else if (!strcmp(c, "progress"))
        test_progress();
    else if (!strcmp(c, "ending"))
        test_ending();
    else if (!strcmp(c, "credits"))
        test_credits();
    else if (!strcmp(c, "wrap"))
        test_wrap();
    else {
        fprintf(stderr, "unknown case %s\n", c);
        return 1;
    }
    bast_map_free(loaded);
    return 0;
}
