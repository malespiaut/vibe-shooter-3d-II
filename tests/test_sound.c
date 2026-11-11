/* Sound events (issue #118, game/sound.h).
 *   test_sound unit <maps dir>          queue, names, hash, door/use sounds
 *   test_sound trace <maps dir> <demo>  replays a demo in-process and prints
 *       one line per tic: `tic use keys` (use = BT_USE in that tic's cmd,
 *       keys = player key bits after it). tests/test_sound_events.py uses
 *       it as the game-state oracle for the --sound-log checks. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/demo.h"
#include "game/items.h"
#include "game/sound.h"
#include "game/state.h"

#define FX(v) ((fix_t)((v) * 65536))

static game_state gs;

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    assert(buf && fread(buf, 1, (size_t)n, f) == (size_t)n);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static bast_map *load_map(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.vmap", dir, name);
    bast_map *m = bast_map_load(path);
    assert(m);
    return m;
}

/* Index of the first queued event with `id`, or -1. */
static int find(int id)
{
    for (int i = 0; i < gs.sounds.count; i++)
        if (gs.sounds.ev[i].id == id)
            return i;
    return -1;
}

/* Puts the player at the centre of tile (tx, ty), facing `angle`, and runs
 * one tic pressing use. */
static void use_from(int tx, int ty, angle_t angle)
{
    gs.player.x = FX(tx) + FX(0.5);
    gs.player.y = FX(ty) + FX(0.5);
    gs.player.angle = angle;
    gs.prev_buttons = 0;
    ticcmd use = { 0, 0, 0, BT_USE };
    game_tic(&gs, &use);
}

static void test_unit(const char *maps)
{
    assert(SFX_COUNT == 76);
    assert(strcmp(sfx_name(SFX_door_open), "door_open") == 0);
    assert(strcmp(sfx_name(0), "barrel_explode") == 0);
    assert(strcmp(sfx_name(SFX_COUNT - 1), "woofu_sight") == 0);
    assert(sfx_name(SFX_COUNT) == NULL && sfx_name(-1) == NULL);

    bast_map *m = load_map(maps, "doortest");
    game_init(&gs, m, 1, 2, 0);

    /* Sound never touches the hash. */
    const uint64_t h = game_hash(&gs);
    game_sound(&gs, SFX_door_open, FX(1), FX(2));
    game_sound_local(&gs, SFX_pistol_fire);
    assert(gs.sounds.count == 2 && game_hash(&gs) == h);
    assert(gs.sounds.ev[0].local == 0 && gs.sounds.ev[0].x == FX(1));
    assert(gs.sounds.ev[1].local == 1);

    /* Overflow drops, out-of-range ids are ignored. */
    for (int i = 0; i < SOUND_QUEUE_MAX + 10; i++)
        game_sound_local(&gs, SFX_noway);
    game_sound_local(&gs, SFX_COUNT);
    assert(gs.sounds.count == SOUND_QUEUE_MAX);

    /* game_tic empties the queue: one quiet tic, no events. */
    ticcmd idle = { 0, 0, 0, 0 };
    game_tic(&gs, &idle);
    assert(gs.sounds.count == 0);

    /* Plain door D (2,4): door_open at the door centre on the use tic. */
    use_from(2, 3, ANG90);
    int i = find(SFX_door_open);
    assert(i >= 0 && gs.sounds.ev[i].x == FX(2.5) && gs.sounds.ev[i].y == FX(4.5));
    assert(gs.doors[door_at(&gs, 2, 4)].state == DOOR_OPENING);
    /* Using it again while it opens: nothing. */
    use_from(2, 3, ANG90);
    assert(gs.sounds.count == 0);

    /* Green door G (4,4) without the key: door_locked, stays shut. */
    use_from(4, 3, ANG90);
    assert(find(SFX_door_locked) >= 0 && find(SFX_door_open) < 0);
    assert(gs.doors[door_at(&gs, 4, 4)].state == DOOR_CLOSED);
    /* With the key: opens. */
    game_give_keys(&gs, KEY_GREEN);
    use_from(4, 3, ANG90);
    assert(find(SFX_door_open) >= 0 && find(SFX_door_locked) < 0);

    /* ONEWAY door O (6,4) from the wrong (north) side: noway. */
    use_from(6, 5, ANG270);
    assert(find(SFX_noway) >= 0 && find(SFX_door_open) < 0);

    /* A plain wall: noway. Nothing in reach: silent. */
    use_from(1, 1, ANG180); /* x = 0 is the outer wall */
    assert(find(SFX_noway) >= 0);
    use_from(5, 1, 0);
    assert(gs.sounds.count == 0);

    /* Plain door D closes by itself: door_close once it starts closing. */
    gs.player.x = FX(8.5);
    gs.player.y = FX(1.5);
    int closed = 0;
    for (int t = 0; t < 400 && !closed; t++) {
        game_tic(&gs, &idle);
        closed = find(SFX_door_close) >= 0;
    }
    assert(closed);

    /* Weapons: switch, then fire. No enemies here: the Claws swish. */
    weapon_give(&gs.player.weapons, WP_REPEATER);
    ticcmd sel = { 0, 0, 0, (uint8_t)(WP_REPEATER << BT_WEAPON_SHIFT) };
    game_tic(&gs, &sel);
    assert(find(SFX_weapon_switch) >= 0);
    game_tic(&gs, &sel); /* same slot again: no second switch sound */
    assert(find(SFX_weapon_switch) < 0);
    ticcmd fire = { 0, 0, 0, BT_FIRE };
    int shots = 0;
    for (int t = 0; t < 20; t++) {
        game_tic(&gs, &fire);
        shots += find(SFX_repeater_fire) >= 0;
    }
    assert(shots >= 2); /* every 4 tics once raised (switch takes 10) */
    ticcmd claws = { 0, 0, 0, (uint8_t)(WP_CLAWS << BT_WEAPON_SHIFT) };
    game_tic(&gs, &claws);
    int swish = 0;
    for (int t = 0; t < 20 && !swish; t++) {
        game_tic(&gs, &fire);
        swish = find(SFX_claw_swish) >= 0;
    }
    assert(swish);

    /* Player: pain, Ninth Life saving a lethal hit, then death. */
    gs.sounds.count = 0;
    player_damage(&gs, 10);
    assert(find(SFX_player_pain) >= 0);
    gs.sounds.count = 0;
    gs.player.ninth_life = 1;
    player_damage(&gs, 1000);
    assert(find(SFX_ninth_life) >= 0 && find(SFX_player_death) < 0 && !gs.player.dead);
    gs.sounds.count = 0;
    gs.player.invuln_tics = 0;
    player_damage(&gs, 1000);
    assert(find(SFX_player_death) >= 0 && find(SFX_player_pain) < 0 && gs.player.dead);

    bast_map_free(m);
    printf("sound unit: ok\n");
}

static void trace(const char *maps, const char *path)
{
    char err[256];
    char *text = slurp(path);
    demo_t *d = demo_parse(text, err, sizeof err);
    assert(d);
    bast_map *m = load_map(maps, d->map[0] ? d->map : "m1test");
    game_init(&gs, m, d->level, d->skill, d->seed);
    ticcmd cmd;
    int r;
    while ((r = demo_next(d, &gs, &cmd, err, sizeof err)) == DEMO_TIC) {
        const uint32_t tic = gs.tic;
        game_tic(&gs, &cmd);
        printf("%u %d %u\n", (unsigned)tic, (cmd.buttons & BT_USE) != 0,
               (unsigned)gs.player.keys);
    }
    assert(r == DEMO_END);
    demo_free(d);
    bast_map_free(m);
    free(text);
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "unit") == 0)
        test_unit(argv[2]);
    else if (argc == 4 && strcmp(argv[1], "trace") == 0)
        trace(argv[2], argv[3]);
    else {
        fprintf(stderr, "usage: test_sound unit <maps> | trace <maps> <demo>\n");
        return 1;
    }
    return 0;
}
