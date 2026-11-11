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
 *    Woofu (#212, facing the camera at distance 10): `phase` frames 0 and 5
 *    and the woofu2 (form 1) idle and walk frames draw, woofu2 differs
 *    from woofu (no lid); form 1 death and other enemies' form 1 / phase
 *    frames are not drawn.
 * 1b. thinker_anim() on Woofu: the 35-tic lid_blast flinch maps to `phase`
 *    frames 0..5, then phase 2 shows woofu2 (form 1) but for death.
 * 5. Incomplete set: on a symlinked copy of the assets (loose files, no
 *    game.pak) without pom_idle_d0_f0.tex, render_init still succeeds, the Pomeranian is
 *    skipped (not drawn, never substituted) and the Chomper still draws.
 *    Without one `phase` frame, or one woofu2 frame, Woofu is not drawn.
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

static void expect_form(const thinker_t *t, int phase, uint32_t tic, int anim, int frame,
                        int form)
{
    int a = -1, f = -1, fm = -1;
    thinker_anim(t, phase, tic, &a, &f, &fm);
    if (a != anim || f != frame || fm != form)
        fprintf(stderr, "state %d tics %d tic %u: got %s f%d form %d, want %s f%d form %d\n",
                t->state, t->tics, tic, sprite_anim_name(a), f, fm, sprite_anim_name(anim), frame,
                form);
    assert(a == anim && f == frame && fm == form);
}

static void expect(const thinker_t *t, uint32_t tic, int anim, int frame)
{
    expect_form(t, 1, tic, anim, frame, 0);
    expect_form(t, 2, tic, anim, frame, 0); /* boss phase: no effect on others */
}

/* Woofu (#212): the lid-blast flinch (PAIN, BOSS_FLINCH_TICS) plays the
 * 6 `phase` frames; afterwards, in phase 2, everything but death comes from
 * the lidless woofu2 set (form 1). */
static void check_boss_anim(void)
{
    static thinkers_t th;
    memset(&th, 0, sizeof th);
    thinker_t *b = thinker_get(&th, thinker_spawn(&th, BAST_ENT_WOOFU, 0, 0, 0));
    assert(b);
    expect_form(b, 1, 0, SPR_IDLE, 0, 0);
    expect_form(b, 1, 18, SPR_IDLE, 1, 0);
    b->state = TH_STATE_CHASE;
    b->movedir = DI_EAST;
    expect_form(b, 1, 6, SPR_WALK, 1, 0);

    /* The flinch: tics 35 (the hit) .. 1, frames 0..5 in equal steps. */
    b->state = TH_STATE_PAIN;
    static const struct { int tics, frame; } fl[] = {
        { 35, 0 }, { 30, 0 }, { 29, 1 }, { 24, 1 }, { 23, 2 }, { 18, 2 },
        { 17, 3 }, { 12, 3 }, { 11, 4 }, { 6, 4 }, { 5, 5 }, { 1, 5 },
    };
    for (size_t i = 0; i < sizeof fl / sizeof fl[0]; i++) {
        b->tics = fl[i].tics;
        expect_form(b, 2, 500, SPR_PHASE, fl[i].frame, 0);
    }
    assert(BOSS_FLINCH_TICS == 35);

    /* After it: woofu2 for idle, walk, windup, attack. */
    b->state = TH_STATE_CHASE;
    b->tics = 0;
    expect_form(b, 2, 6, SPR_WALK, 1, 1);
    b->movedir = DI_NONE;
    expect_form(b, 2, 6, SPR_WALK, 0, 1);
    b->state = TH_STATE_STAND;
    expect_form(b, 2, 18, SPR_IDLE, 1, 1);
    b->state = TH_STATE_WINDUP;
    b->tics = 1;
    expect_form(b, 2, 0, SPR_WINDUP, 1, 1);
    b->state = TH_STATE_ATTACK;
    b->tics = AI_ATTACK_TICS;
    expect_form(b, 2, 0, SPR_ATTACK, 0, 1);
    b->state = TH_STATE_ATTACK;
    expect_form(b, 1, 0, SPR_ATTACK, 0, 0); /* phase 1: the lidded set */

    /* Death: woofu's own (lidless) death frames, form 0. */
    b->state = TH_STATE_CHASE;
    b->health = 1;
    assert(thinker_damage(&th, b, 1));
    b->died_tic = 0;
    expect_form(b, 2, 0, SPR_DEATH, 0, 0);
    expect_form(b, 2, 100, SPR_DEATH, 4, 0);
    puts("thinker_anim boss: ok");
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
    static const int counts[SPR_ANIMS] = { 2, 4, 2, 2, 1, 5, 8, 6 };
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
    assert(strcmp(sprite_anim_name(SPR_PHASE), "phase") == 0);
    assert(sprite_def_for_entity(BAST_ENT_WOOFU)->phase);
    assert(strcmp(sprite_def_for_entity(BAST_ENT_WOOFU)->form2, "woofu2") == 0);
    for (int type = BAST_ENT_MUTT_TROOPER; type < BAST_ENT_WOOFU; type++)
        assert(!sprite_def_for_entity(type)->phase && !sprite_def_for_entity(type)->form2);
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
        const render_thing one = { .type = (uint16_t)type, .x = (fix_t)(5.5 * 65536),
                                   .y = (fix_t)(3.5 * 65536), .anim = SPR_IDLE };
        render_view_things(m, gs.player.x, gs.player.y, gs.player.angle, &one, 1, gs.tic, fb);
        const int px = diff_count(base, fb, 100, 220);
        printf("%s idle f0: %d px\n", sprite_name_for_entity(type), px);
        assert(px > 0);
        if (type == BAST_ENT_SHEPHERD_SHIELDBEARER)
            assert(px > 40);
    }
    /* Woofu's `phase` frames and the woofu2 (form 1) set draw, woofu2
     * differs from woofu (no lid); form 1 has
     * no death (NULL, not drawn) and other enemies have no form 1. */
    static uint8_t fb_phase[SCREEN_W * SCREEN_H], fb_idle2[SCREEN_W * SCREEN_H];
    const render_thing boss[] = {
        { .type = BAST_ENT_WOOFU, .anim = SPR_PHASE, .frame = 0 },
        { .type = BAST_ENT_WOOFU, .anim = SPR_PHASE, .frame = 5 },
        { .type = BAST_ENT_WOOFU, .anim = SPR_IDLE, .form = 1 },
        { .type = BAST_ENT_WOOFU, .anim = SPR_WALK, .frame = 3, .form = 1 },
        { .type = BAST_ENT_WOOFU, .anim = SPR_IDLE, .form = 0 },
        { .type = BAST_ENT_WOOFU, .anim = SPR_DEATH, .form = 1 },
        { .type = BAST_ENT_CHIHUAHUA_CHOMPER, .anim = SPR_IDLE, .form = 1 },
        { .type = BAST_ENT_CHIHUAHUA_CHOMPER, .anim = SPR_PHASE },
    };
    int px_boss[8];
    for (int i = 0; i < 8; i++) {
        render_thing one = boss[i];
        one.x = (fix_t)(12.5 * 65536); /* distance 10: the lid is in view */
        one.y = (fix_t)(3.5 * 65536);
        one.angle = ANG180; /* faces the camera (d0): the lid is on its front */
        uint8_t *out = i == 0 ? fb_phase : i == 2 ? fb_idle2 : fb;
        render_view_things(m, gs.player.x, gs.player.y, gs.player.angle, &one, 1, gs.tic, out);
        px_boss[i] = diff_count(base, out, 100, 220);
        if (i == 4)
            memcpy(fb2, out, sizeof fb2);
    }
    printf("woofu phase f0: %d px, phase f5: %d px, woofu2 idle f0: %d px, woofu2 walk f3: %d px;"
           " woofu2 vs woofu idle: %d px differ, woofu phase f0 vs woofu idle: %d px differ;"
           " woofu2 death %d px, chomp form 1 %d px, chomp phase %d px\n",
           px_boss[0], px_boss[1], px_boss[2], px_boss[3], diff_count(fb_idle2, fb2, 100, 220),
           diff_count(fb_phase, fb2, 100, 220), px_boss[5], px_boss[6], px_boss[7]);
    assert(px_boss[0] > 0 && px_boss[1] > 0 && px_boss[2] > 0 && px_boss[3] > 0);
    assert(diff_count(fb_idle2, fb2, 100, 220) > 0); /* the lid is gone */
    assert(px_boss[5] == 0 && px_boss[6] == 0 && px_boss[7] == 0);
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

/* Woofu with one frame of its `phase` or woofu2 set missing (symlinked
 * assets, loose files): render_init succeeds, the whole boss is skipped
 * (no form is drawn, never substituted). */
static void check_boss_incomplete(const char *assets, const char *drop)
{
    char abs[4096], tmpl[] = "/tmp/wse-XXXXXX", cmd[9000];
    assert(realpath(assets, abs));
    assert(mkdtemp(tmpl));
    snprintf(cmd, sizeof cmd,
             "cp -rs '%s' '%s/a' && rm -f '%s/a/game.pak' && rm '%s/a/sprites/%s.tex'", abs,
             tmpl, tmpl, tmpl, drop);
    assert(system(cmd) == 0);
    char dir[4200];
    snprintf(dir, sizeof dir, "%s/a", tmpl);

    bast_map *m = make_map();
    add(m, BAST_ENT_WOOFU, 1.5, 1.5);
    assert(render_init(m, dir));
    static game_state gs;
    game_init(&gs, m, 1, 2, 0);
    render_view(m, gs.player.x, gs.player.y, gs.player.angle, base);
    int px = 0;
    for (int form = 0; form < SPRITE_FORMS; form++) {
        const render_thing one = { .type = BAST_ENT_WOOFU, .x = (fix_t)(12.5 * 65536),
                                   .y = (fix_t)(3.5 * 65536), .angle = ANG180,
                                   .anim = SPR_IDLE, .form = (uint8_t)form };
        render_view_things(m, gs.player.x, gs.player.y, gs.player.angle, &one, 1, gs.tic, fb);
        px += diff_count(base, fb, 100, 220);
    }
    printf("woofu without %s: %d px drawn\n", drop, px);
    assert(px == 0);
    render_shutdown();
    bast_map_free(m);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmpl);
    assert(system(cmd) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    check_anim();
    check_boss_anim();
    check_table();
    if (!colormap_load(argv[1]))
        fprintf(stderr, "%s\n", colormap_error());
    assert(colormap_load(argv[1]));
    assert(files_init(argv[1]));
    check_live(argv[1]);
    check_all_types(argv[1]);
    check_incomplete(argv[1]);
    check_boss_incomplete(argv[1], "woofu_phase_d0_f3");
    check_boss_incomplete(argv[1], "woofu2_walk_d0_f2");
    puts("world_sprites_enemies: ok");
    return 0;
}
