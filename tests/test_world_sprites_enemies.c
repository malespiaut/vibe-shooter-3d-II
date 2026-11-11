/* world_sprites_enemies (issue #146): enemies drawn as animated
 * 8-direction world sprites from live AI state.
 *
 * 1. thinker_anim() on a synthetic Chomper: idle at tics 0/18, walk (and
 *    frame 0 while not stepping), windup (glow frame in the last half),
 *    attack, pain, death at tics 0/5/20/200 after the kill (frame 4, the
 *    corpse, stays) and gib at tic 100 (frame 7).
 * 2. Every enemy type has a live enemy sprite_def; the Chomper's sprite
 *    set (on main) has every animation of enemies.md.
 * 3. Live state, on a synthetic map (camera (2.5, 3.5) facing east): a
 *    Chomper at (5.5, 3.5) is drawn by render_view_things from
 *    ai_render_things, its corpse after a killing hit too, and a Pomeranian
 *    at (7.5, 5.5) is drawn as well; rendering leaves game_hash() unchanged.
 * 4. All 7 enemy types on one map: render_init prints no "incomplete"
 *    warning (stderr captured), and each type's idle frame draws at
 *    distance 3: Shepherd ("shep") > 40 px, Woofu (no gib set) > 0 px.
 * 5. Incomplete set: on a symlinked copy of the assets (loose files, no
 *    game.pak) without pom_idle_d0_f0.tex, render_init still succeeds, the Pomeranian is
 *    skipped (not drawn, never substituted) and the Chomper still draws.
 *
 *   test_world_sprites_enemies <assets/gen dir> */
#define _XOPEN_SOURCE 700 /* realpath, mkdtemp */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bast.h"
#include "engine/colormap.h"
#include "engine/files.h"
#include "engine/render.h"
#include "engine/sprite.h"
#include "engine/video.h"
#include "game/ai.h"
#include "game/enemy_info.h"
#include "game/state.h"
#include "game/thinker.h"

static uint8_t base[SCREEN_W * SCREEN_H], fb[SCREEN_W * SCREEN_H], fb2[SCREEN_W * SCREEN_H];
static render_thing things[RENDER_MAX_SPRITES];

static void expect(const thinker_t *t, uint32_t tic, int anim, int frame)
{
    int a = -1, f = -1;
    thinker_anim(t, tic, &a, &f);
    if (a != anim || f != frame)
        fprintf(stderr, "state %d tic %u: got %s f%d, want %s f%d\n", t->state, tic,
                sprite_anim_name(a), f, sprite_anim_name(anim), frame);
    assert(a == anim && f == frame);
}

static void check_anim(void)
{
    static thinkers_t th;
    memset(&th, 0, sizeof th);
    thinker_t *t = thinker_get(&th, thinker_spawn(&th, BAST_ENT_CHIHUAHUA_CHOMPER, 0, 0, 0));
    assert(t);

    expect(t, 0, SPR_IDLE, 0);
    expect(t, 17, SPR_IDLE, 0);
    expect(t, 18, SPR_IDLE, 1);
    expect(t, 36, SPR_IDLE, 0);

    t->state = TH_STATE_CHASE;
    t->movedir = DI_NONE;
    expect(t, 13, SPR_WALK, 0); /* not stepping: stands in walk frame 0 */
    t->movedir = DI_EAST;
    expect(t, 0, SPR_WALK, 0);
    expect(t, 6, SPR_WALK, 1);
    expect(t, 12, SPR_WALK, 2);
    expect(t, 18, SPR_WALK, 3);
    expect(t, 24, SPR_WALK, 0);

    const int32_t w = enemy_info(t->type)->windup;
    t->state = TH_STATE_WINDUP;
    t->tics = w;
    expect(t, 0, SPR_WINDUP, 0);
    t->tics = w / 2;
    expect(t, 0, SPR_WINDUP, 1);
    t->tics = 1;
    expect(t, 0, SPR_WINDUP, 1);

    t->state = TH_STATE_ATTACK;
    t->tics = AI_ATTACK_TICS;
    expect(t, 0, SPR_ATTACK, 0);
    t->tics = 1;
    expect(t, 0, SPR_ATTACK, 1);

    t->state = TH_STATE_PAIN;
    t->tics = AI_PAIN_TICS;
    expect(t, 0, SPR_PAIN, 0);

    /* Killed at tic 1000 by a plain hit: death 0..4 at 5 tics, then corpse. */
    t->state = TH_STATE_CHASE;
    t->health = 1;
    assert(thinker_damage(&th, t, 1));
    t->died_tic = 1000;
    expect(t, 1000, SPR_DEATH, 0);
    expect(t, 1005, SPR_DEATH, 1);
    expect(t, 1020, SPR_DEATH, 4);
    expect(t, 1200, SPR_DEATH, 4);
    expect(t, 900000, SPR_DEATH, 4);

    /* Gibbed (HP <= -(max / 2)): gib 0..7 at 4 tics, the last one stays. */
    thinker_t *g = thinker_get(&th, thinker_spawn(&th, BAST_ENT_CHIHUAHUA_CHOMPER, 0, 0, 0));
    assert(thinker_damage(&th, g, g->max_health + g->max_health / 2));
    assert(g->flags & TH_FLAG_GIB);
    g->died_tic = 0;
    expect(g, 0, SPR_GIB, 0);
    expect(g, 4, SPR_GIB, 1);
    expect(g, 28, SPR_GIB, 7);
    expect(g, 100, SPR_GIB, 7);

    /* The boss has no gib (enemies.md): overkill plays its death. */
    thinker_t *b = thinker_get(&th, thinker_spawn(&th, BAST_ENT_WOOFU, 0, 0, 0));
    assert(b && thinker_damage(&th, b, b->max_health * 2));
    assert(!(b->flags & TH_FLAG_GIB));
    b->died_tic = 0;
    expect(b, 100, SPR_DEATH, 4);
    puts("thinker_anim: ok");
}

static void check_table(void)
{
    static const int counts[SPR_ANIMS] = { 2, 4, 2, 2, 1, 5, 8 };
    for (int a = 0; a < SPR_ANIMS; a++)
        assert(sprite_enemy_frames(a) == counts[a] && sprite_enemy_frames(a) <= SPRITE_MAX_FRAMES);
    assert(strcmp(sprite_anim_name(SPR_WINDUP), "windup") == 0);
    assert(strcmp(sprite_anim_name(SPR_GIB), "gib") == 0);
    for (int type = BAST_ENT_MUTT_TROOPER; type <= BAST_ENT_WOOFU; type++) {
        const sprite_def *d = sprite_def_for_entity(type);
        assert(d && d->live && d->enemy && enemy_info((uint16_t)type));
    }
    assert(strcmp(sprite_name_for_entity(BAST_ENT_CHIHUAHUA_CHOMPER), "chomp") == 0);
    assert(strcmp(sprite_name_for_entity(BAST_ENT_MUTT_TROOPER), "mutt") == 0);
    assert(strcmp(sprite_name_for_entity(BAST_ENT_SHEPHERD_SHIELDBEARER), "shep") == 0);
    assert(sprite_def_for_entity(BAST_ENT_WOOFU)->no_gib);
}

static int add(bast_map *m, int type, double x, double y)
{
    int i = bast_map_add_entity(m);
    assert(i >= 0);
    bast_map_entity *e = &m->entities[i];
    e->type = (uint16_t)type;
    e->x = (int32_t)(x * 65536);
    e->y = (int32_t)(y * 65536);
    e->flags = BAST_ENT_SKILL_ALL | BAST_ENT_AMBUSH;
    return i;
}

/* Number of view pixels in columns x0..x1-1 where a and b differ. */
static int diff_count(const uint8_t *a, const uint8_t *b, int x0, int x1)
{
    int n = 0;
    for (int y = 0; y < VIEW_H; y++)
        for (int x = x0; x < x1; x++)
            n += a[y * SCREEN_W + x] != b[y * SCREEN_W + x];
    return n;
}

static void view(const game_state *gs, uint8_t *out)
{
    const int n = ai_render_things(gs, things, RENDER_MAX_SPRITES);
    render_view_things(gs->map, gs->player.x, gs->player.y, gs->player.angle, things, n, gs->tic,
                       out);
}

static bast_map *make_map(void)
{
    bast_map *m = bast_map_new(16, 8);
    assert(m);
    const int t1 = bast_map_texture_index(m, "test01");
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 16; x++)
            if (x == 0 || y == 0 || x == 15 || y == 7) {
                bast_map_cell *c = bast_map_cell_at(m, x, y);
                c->wall = (uint8_t)t1;
                c->flags = BAST_CELL_SOLID;
            }
    add(m, BAST_ENT_PLAYER_START, 2.5, 3.5);
    add(m, BAST_ENT_CHIHUAHUA_CHOMPER, 5.5, 3.5);         /* dead ahead, dist 3 */
    add(m, BAST_ENT_POMERANIAN_PLASMAGUNNER, 7.5, 5.5);   /* left of the Chomper */
    return m;
}

static void check_live(const char *assets)
{
    bast_map *m = make_map();
    if (!render_init(m, assets))
        fprintf(stderr, "%s\n", render_error());
    assert(render_init(m, assets));

    static game_state gs;
    game_init(&gs, m, 1, 2, 0);
    assert(ai_render_things(&gs, things, RENDER_MAX_SPRITES) == 2);
    assert(things[0].type == BAST_ENT_CHIHUAHUA_CHOMPER && things[0].anim == SPR_IDLE);

    const uint64_t h0 = game_hash(&gs);
    render_view(m, gs.player.x, gs.player.y, gs.player.angle, base);
    view(&gs, fb);
    assert(game_hash(&gs) == h0);
    const int chomp = diff_count(base, fb, 140, 180);
    const int pom = diff_count(base, fb, 0, 140);
    printf("chomper idle: %d px drawn; pomeranian idle: %d px\n", chomp, pom);
    assert(chomp > 40);
    assert(pom > 40);

    /* Kill it with a plain hit: death frames, then the corpse stays. */
    thinker_t *t = &gs.things.list[0];
    assert(t->type == BAST_ENT_CHIHUAHUA_CHOMPER);
    assert(ai_damage(&gs, t, t->health));
    assert(t->state == TH_STATE_DEAD && t->died_tic == gs.tic && !(t->flags & TH_FLAG_GIB));
    ai_render_things(&gs, things, RENDER_MAX_SPRITES);
    assert(things[0].anim == SPR_DEATH && things[0].frame == 0);
    gs.tic += 200;
    ai_render_things(&gs, things, RENDER_MAX_SPRITES);
    assert(things[0].anim == SPR_DEATH && things[0].frame == 4);
    const uint64_t h1 = game_hash(&gs);
    view(&gs, fb2);
    assert(game_hash(&gs) == h1);
    const int corpse = diff_count(base, fb2, 140, 180);
    printf("chomper corpse: %d px drawn, %d px differ from idle\n", corpse,
           diff_count(fb, fb2, 140, 180));
    assert(corpse > 0 && diff_count(fb, fb2, 140, 180) > 0);

    render_shutdown();
    bast_map_free(m);
}

/* All 7 enemy types on one map: no "incomplete" warning from render_init on
 * main's assets, and every type's idle d0 frame draws in front of the
 * camera. */
static void check_all_types(const char *assets)
{
    bast_map *m = make_map();
    for (int type = BAST_ENT_MUTT_TROOPER; type <= BAST_ENT_WOOFU; type++)
        add(m, type, 1.5, 1.5); /* behind the camera: only loaded here */

    char log[] = "/tmp/wse-log-XXXXXX";
    const int fd = mkstemp(log);
    assert(fd >= 0);
    fflush(stderr);
    const int saved = dup(2);
    assert(saved >= 0 && dup2(fd, 2) == 2);
    const int ok = render_init(m, assets);
    fflush(stderr);
    assert(dup2(saved, 2) == 2);
    close(saved);
    close(fd);
    if (!ok)
        fprintf(stderr, "%s\n", render_error());
    assert(ok);
    static char text[4096];
    FILE *f = fopen(log, "r");
    assert(f);
    const size_t len = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[len] = 0;
    unlink(log);
    printf("render_init, all 7 enemy types: stderr %zu bytes%s%s\n", len, len ? ": " : "", text);
    assert(!strstr(text, "incomplete"));

    static game_state gs;
    game_init(&gs, m, 1, 2, 0);
    render_view(m, gs.player.x, gs.player.y, gs.player.angle, base);
    for (int type = BAST_ENT_MUTT_TROOPER; type <= BAST_ENT_WOOFU; type++) {
        const render_thing one = { (uint16_t)type, (fix_t)(5.5 * 65536), (fix_t)(3.5 * 65536),
                                   0, SPR_IDLE, 0 };
        render_view_things(m, gs.player.x, gs.player.y, gs.player.angle, &one, 1, gs.tic, fb);
        const int px = diff_count(base, fb, 100, 220);
        printf("%s idle f0: %d px\n", sprite_name_for_entity(type), px);
        assert(px > 0);
        if (type == BAST_ENT_SHEPHERD_SHIELDBEARER)
            assert(px > 40);
    }
    render_shutdown();
    bast_map_free(m);
}

/* Incomplete set: a symlinked copy of the assets without the Pomeranian's
 * idle d0 frame. render_init warns but succeeds; the Pom is not drawn. */
static void check_incomplete(const char *assets)
{
    char abs[4096], tmpl[] = "/tmp/wse-XXXXXX", cmd[9000];
    assert(realpath(assets, abs));
    assert(mkdtemp(tmpl));
    snprintf(cmd, sizeof cmd,
             "cp -rs '%s' '%s/a' && rm -f '%s/a/game.pak'"
             " && rm '%s/a/sprites/pom_idle_d0_f0.tex'",
             abs, tmpl, tmpl, tmpl);
    assert(system(cmd) == 0);
    char dir[4200];
    snprintf(dir, sizeof dir, "%s/a", tmpl);

    bast_map *m = make_map();
    if (!render_init(m, dir))
        fprintf(stderr, "%s\n", render_error());
    assert(render_init(m, dir)); /* missing Pom set: warning, not an error */
    static game_state gs;
    game_init(&gs, m, 1, 2, 0);
    render_view(m, gs.player.x, gs.player.y, gs.player.angle, base);
    view(&gs, fb);
    const int chomp = diff_count(base, fb, 140, 180);
    const int pom = diff_count(base, fb, 0, 140);
    printf("incomplete pom set: chomper %d px, pomeranian %d px\n", chomp, pom);
    assert(chomp > 40);
    assert(pom == 0);
    render_shutdown();
    bast_map_free(m);

    snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmpl);
    assert(system(cmd) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    check_anim();
    check_table();
    if (!colormap_load(argv[1]))
        fprintf(stderr, "%s\n", colormap_error());
    assert(colormap_load(argv[1]));
    assert(files_init(argv[1]));
    check_live(argv[1]);
    check_all_types(argv[1]);
    check_incomplete(argv[1]);
    puts("world_sprites_enemies: ok");
    return 0;
}
