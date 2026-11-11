/* RESPAWN_ITEM entities (tech.md Map entities, levels.md 8 set piece 3,
 * issue #192). test_respawn <group>: present, timing, tag_absent,
 * tag_present, items, invalid, hash. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "game/items.h"
#include "game/state.h"

#define FX(i) fix_from_int(i)
#define HALF (FIX_ONE / 2)
#define TAG 5

static bast_map *g_map;

static bast_map *open_map(void)
{
    bast_map *m = bast_map_new(16, 16);
    assert(m);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            if (x == 0 || y == 0 || x == 15 || y == 15)
                bast_map_cell_at(m, x, y)->flags = BAST_CELL_SOLID;
    return m;
}

static int add(bast_map *m, int type, int tx, int ty, uint8_t tag, uint8_t param)
{
    int i = bast_map_add_entity(m);
    assert(i >= 0);
    bast_map_entity *e = &m->entities[i];
    e->type = (uint16_t)type;
    e->x = FX(tx) + HALF;
    e->y = FX(ty) + HALF;
    e->flags = BAST_ENT_SKILL_ALL;
    e->tag = tag;
    e->param = param;
    return i;
}

/* Player start at (1,1), a Sun Shard alcove at (5,5) stopped by TAG, and a
 * plain Fish Snack at (10,10). */
static bast_map *test_map(int param)
{
    bast_map *m = open_map();
    add(m, BAST_ENT_PLAYER_START, 1, 1, 0, 0);
    add(m, BAST_ENT_RESPAWN_ITEM, 5, 5, TAG, (uint8_t)param);
    add(m, BAST_ENT_FISH_SNACK, 10, 10, 0, 0);
    return m;
}

static const ticcmd idle = { 0, 0, 0, 0 };

/* The respawn pickup (the first pickup_t: entity order). */
static pickup_t *alcove(game_state *gs)
{
    assert(gs->pickup_count == 2);
    pickup_t *pk = &gs->pickups[0];
    assert(pk->type == BAST_ENT_SUN_SHARD);
    return pk;
}

/* Present = drawn and touchable: in items_render_things. */
static int present(const game_state *gs)
{
    render_thing th[8];
    const int n = items_render_things(gs, th, 8);
    for (int i = 0; i < n; i++)
        if (th[i].type == BAST_ENT_SUN_SHARD)
            return 1;
    return 0;
}

static void stand_on_alcove(game_state *gs)
{
    gs->player.x = FX(5) + HALF;
    gs->player.y = FX(5) + HALF;
}

static void stand_away(game_state *gs)
{
    gs->player.x = FX(1) + HALF;
    gs->player.y = FX(1) + HALF;
}

/* Takes the alcove item; returns the state tic of the tic that took it. */
static uint32_t take(game_state *gs)
{
    gs->player.weapons.ammo[AM_CELLS] = 0;
    stand_on_alcove(gs);
    const uint32_t t = gs->tic;
    game_tic(gs, &idle);
    /* Sun Shard: 20 cells, 30 on Kitten (1.5x). */
    assert(gs->player.weapons.ammo[AM_CELLS] == (gs->skill == 1 ? 30 : 20));
    assert(!present(gs));
    stand_away(gs);
    return t;
}

static void t_present(void)
{
    game_state gs;
    game_init(&gs, g_map, 8, 2, 0);
    assert(gs.tic == 0 && present(&gs));
    assert(alcove(&gs)->x == FX(5) + HALF && alcove(&gs)->y == FX(5) + HALF);
    /* Every skill. */
    game_init(&gs, g_map, 8, 1, 0);
    assert(present(&gs));
    game_init(&gs, g_map, 8, 3, 0);
    assert(present(&gs));
    puts("respawn_present: ok");
}

static void t_timing(void)
{
    game_state gs;
    game_init(&gs, g_map, 8, 2, 0);
    for (int i = 0; i < 17; i++)
        game_tic(&gs, &idle);
    for (int round = 0; round < 3; round++) {
        const uint32_t t = take(&gs);
        /* Absent through the tic T+1049, back in the tic T+1050. */
        while (gs.tic < t + RESPAWN_TICS) {
            game_tic(&gs, &idle);
            assert(!present(&gs));
        }
        assert(gs.tic == t + RESPAWN_TICS && !present(&gs));
        game_tic(&gs, &idle);
        assert(gs.tic == t + RESPAWN_TICS + 1 && present(&gs));
        /* Untaken, it just stays. */
        for (int i = 0; i < 3000; i++)
            game_tic(&gs, &idle);
        assert(present(&gs));
    }
    /* Skill does not change the delay. */
    for (int skill = 1; skill <= 3; skill++) {
        game_init(&gs, g_map, 8, skill, 0);
        const uint32_t t = take(&gs);
        while (gs.tic < t + RESPAWN_TICS)
            game_tic(&gs, &idle);
        assert(!present(&gs));
        game_tic(&gs, &idle);
        assert(present(&gs));
    }
    puts("respawn_timing: ok");
}

static void t_tag_absent(void)
{
    game_state gs;
    game_init(&gs, g_map, 8, 2, 0);
    take(&gs);
    for (int i = 0; i < 500; i++)
        game_tic(&gs, &idle);
    game_fire_tag(&gs, TAG);
    for (int i = 0; i < 5000; i++)
        game_tic(&gs, &idle);
    assert(!present(&gs));
    /* Standing on the spot gives nothing. */
    gs.player.weapons.ammo[AM_CELLS] = 0;
    stand_on_alcove(&gs);
    game_tic(&gs, &idle);
    assert(gs.player.weapons.ammo[AM_CELLS] == 0);
    /* Another tag does not stop it. */
    game_init(&gs, g_map, 8, 2, 0);
    take(&gs);
    game_fire_tag(&gs, TAG + 1);
    for (int i = 0; i < RESPAWN_TICS + 1; i++)
        game_tic(&gs, &idle);
    assert(present(&gs));
    puts("respawn_tag_absent: ok");
}

static void t_tag_present(void)
{
    game_state gs;
    game_init(&gs, g_map, 8, 2, 0);
    game_tic(&gs, &idle);
    game_fire_tag(&gs, TAG);
    for (int i = 0; i < 2000; i++)
        game_tic(&gs, &idle);
    assert(present(&gs));
    take(&gs);
    for (int i = 0; i < 5000; i++)
        game_tic(&gs, &idle);
    assert(!present(&gs));
    /* Also after a respawn: present when the tag fires, gone once taken. */
    game_init(&gs, g_map, 8, 2, 0);
    take(&gs);
    for (int i = 0; i < RESPAWN_TICS + 1; i++)
        game_tic(&gs, &idle);
    assert(present(&gs));
    game_fire_tag(&gs, TAG);
    take(&gs);
    for (int i = 0; i < 5000; i++)
        game_tic(&gs, &idle);
    assert(!present(&gs));
    puts("respawn_tag_present: ok");
}

static void t_items(void)
{
    game_state gs;
    game_init(&gs, g_map, 8, 2, 0);
    assert(gs.items_total == 2 && gs.items_got == 0);
    take(&gs);
    assert(gs.items_got == 1);
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < RESPAWN_TICS + 1; i++)
            game_tic(&gs, &idle);
        uint32_t seq = gs.event_seq;
        take(&gs);
        /* Normal pickup event (message + sound), but not counted. */
        assert(gs.event_seq == seq + 1);
        assert(gs.events[seq % GAME_EVENT_RING].kind == GEV_PICKUP);
        assert(gs.events[seq % GAME_EVENT_RING].type == BAST_ENT_SUN_SHARD);
        assert(gs.items_got == 1 && gs.items_total == 2);
    }
    puts("respawn_items: ok");
}

static void t_invalid(void)
{
    /* Anything but a non-weapon pickup type 48..68 is rejected at load. */
    const int bad[] = { 0, 1, BAST_ENT_RESPAWN_ITEM, BAST_ENT_MUTT_TROOPER,
                        BAST_ENT_ANKH_PISTOL, BAST_ENT_SPHINX_HOWLER, 69, 95,
                        BAST_ENT_KIBBLE_BARREL, 255 };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        bast_map *m = test_map(bad[i]);
        game_state gs;
        game_init(&gs, m, 8, 2, 0);
        assert(gs.pickup_count == 1 && gs.pickups[0].type == BAST_ENT_FISH_SNACK);
        assert(gs.items_total == 1 && gs.things.count == 0);
        bast_map_free(m);
    }
    const int good[] = { BAST_ENT_FISH_SNACK, BAST_ENT_SCARAB, BAST_ENT_NINTH_LIFE };
    for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) {
        bast_map *m = test_map(good[i]);
        game_state gs;
        game_init(&gs, m, 8, 2, 0);
        assert(gs.pickup_count == 2 && gs.pickups[0].type == good[i]);
        bast_map_free(m);
    }
    puts("respawn_invalid: ok");
}

static void t_hash(void)
{
    /* The countdown is in the hash: two states that differ only in it. */
    game_state a, b;
    game_init(&a, g_map, 8, 2, 0);
    take(&a);
    b = a;
    assert(game_hash(&a) == game_hash(&b));
    b.pickups[0].respawn_tics--;
    assert(game_hash(&a) != game_hash(&b));
    b = a;
    game_fire_tag(&b, TAG);
    b.tags_fired = a.tags_fired; /* only the respawn state differs */
    b.last_tag = a.last_tag;
    assert(game_hash(&a) != game_hash(&b));
    puts("respawn_hash: ok");
}

int main(int argc, char **argv)
{
    g_map = test_map(BAST_ENT_SUN_SHARD);
    const char *g = argc > 1 ? argv[1] : "all";
    const int all = strcmp(g, "all") == 0;
    if (all || !strcmp(g, "present")) t_present();
    if (all || !strcmp(g, "timing")) t_timing();
    if (all || !strcmp(g, "tag_absent")) t_tag_absent();
    if (all || !strcmp(g, "tag_present")) t_tag_present();
    if (all || !strcmp(g, "items")) t_items();
    if (all || !strcmp(g, "invalid")) t_invalid();
    if (all || !strcmp(g, "hash")) t_hash();
    bast_map_free(g_map);
    return 0;
}
