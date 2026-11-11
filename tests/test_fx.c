/* Impact effects (game/fx.h, issue #147): lifetime, ring overflow, a
 * pistol shot spawning a puff at the trace end, and the render list. Maps
 * are built in memory with libbast. Usage: test_fx <group>. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "engine/sprite.h"
#include "game/fx.h"
#include "game/projectile.h"
#include "game/state.h"

#define FX(i) fix_from_int(i)
#define HALF (FIX_ONE / 2)

static const ticcmd IDLE = { 0, 0, 0, 0 };
static const ticcmd FIRE = { 0, 0, 0, BT_FIRE };

/* 16x8 map, solid border, everything else open. */
static bast_map *open_map(void)
{
    bast_map *m = bast_map_new(16, 8);
    assert(m);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 16; x++)
            if (x == 0 || y == 0 || x == 15 || y == 7)
                bast_map_cell_at(m, x, y)->flags = BAST_CELL_SOLID;
    return m;
}

/* Player at (2.5, 3.5) facing east with the pistol. */
static void setup(game_state *gs, const bast_map *m)
{
    game_init(gs, m, 1, 2, 0);
    gs->player.x = FX(2) + HALF;
    gs->player.y = FX(3) + HALF;
    gs->player.angle = 0;
}

/* An effect is drawn after exactly fx_lifetime() tics, then freed. */
static void test_lifetime(void)
{
    bast_map *m = open_map();
    game_state gs;
    setup(&gs, m);
    assert(fx_lifetime(FX_PUFF) == FX_PUFF_FRAMES * FX_TICS_PER_FRAME);
    assert(fx_lifetime(FX_BLOOD) == FX_BLOOD_FRAMES * FX_TICS_PER_FRAME);
    assert(fx_lifetime(FX_NONE) == 0);
    fx_spawn(&gs, FX_NONE, FX(5), FX(3)); /* unknown kind: nothing */
    assert(fx_count(&gs) == 0);

    fx_spawn(&gs, FX_PUFF, FX(5), FX(3));
    const int life = fx_lifetime(FX_PUFF);
    int seen = 1; /* drawn after the spawning tic */
    while (1) {
        game_tic(&gs, &IDLE);
        if (!fx_count(&gs))
            break;
        seen++;
        assert(seen <= life);
    }
    printf("puff: lifetime %d tics, alive for %d\n", life, seen);
    assert(seen == life);
    bast_map_free(m);
}

/* MAX_FX + k spawns: never more than MAX_FX alive, the k oldest replaced. */
static void test_overflow(void)
{
    bast_map *m = open_map();
    game_state gs;
    setup(&gs, m);
    for (int i = 0; i < MAX_FX + 5; i++) {
        fx_spawn(&gs, FX_PUFF, FX(1) + i * 256, FX(3));
        assert(fx_count(&gs) <= MAX_FX);
    }
    assert(fx_count(&gs) == MAX_FX);
    assert(gs.fx.spawned == MAX_FX + 5);
    /* The 5 oldest (x = 1 + i/256 for i < 5) are gone, the newest present. */
    int oldest_left = 0, newest = 0;
    for (int s = 0; s < MAX_FX; s++) {
        const fix_t x = gs.fx.list[s].x;
        if (x < FX(1) + 5 * 256)
            oldest_left++;
        if (x == FX(1) + (MAX_FX + 4) * 256)
            newest++;
    }
    assert(oldest_left == 0 && newest == 1);
    /* Rendered: MAX_FX effects, never past `max`. */
    static render_thing out[RENDER_MAX_SPRITES];
    assert(fx_render_things(&gs, out, 0, RENDER_MAX_SPRITES) == MAX_FX);
    assert(fx_render_things(&gs, out, 0, 10) == 10);
    printf("overflow: %d alive after %u spawns\n", fx_count(&gs), gs.fx.spawned);
    bast_map_free(m);
}

/* One pistol shot east at the wall x = 15: one puff, in the last free tile
 * before the wall the trace ended on, FX_WALL_PULLBACK short of it. */
static void test_pistol_wall(void)
{
    bast_map *m = open_map();
    game_state gs;
    setup(&gs, m);
    game_tic(&gs, &FIRE);
    assert(gs.player.weapons.shots == 1);
    assert(fx_count(&gs) == 1);
    const fx_t *f = NULL;
    for (int i = 0; i < MAX_FX; i++)
        if (gs.fx.list[i].kind != FX_NONE)
            f = &gs.fx.list[i];
    assert(f && f->kind == FX_PUFF);
    /* Spread is at most 255 angle units (1.4 degrees): over 12.5 u the
     * puff stays in row 3. */
    printf("puff at (%d + %d/65536, %d + %d/65536)\n", f->x >> 16, f->x & 0xffff, f->y >> 16,
           f->y & 0xffff);
    assert(fix_to_int(f->x) == 14 && fix_to_int(f->y) == 3);
    assert(f->x >= FX(15) - FX_WALL_PULLBACK - 64 && f->x <= FX(15) - FX_WALL_PULLBACK + 64);
    assert(game_tile_blocks(&gs, fix_to_int(f->x + FX_WALL_PULLBACK + 64), 3));
    /* Rendering lists it as the puff sprite at eye height, frame 0. */
    render_thing out[4];
    assert(fx_render_things(&gs, out, 0, 4) == 1);
    assert(out[0].type == SPRITE_FX_BASE + FXS_PUFF && out[0].frame == 0 && out[0].z == FX_HIT_Z);
    /* Frames advance every FX_TICS_PER_FRAME tics up to the last one. */
    for (int t = 1; t < fx_lifetime(FX_PUFF); t++) {
        game_tic(&gs, &IDLE);
        assert(fx_render_things(&gs, out, 0, 4) == 1);
        assert(out[0].frame == t / FX_TICS_PER_FRAME);
        assert(out[0].frame < FX_PUFF_FRAMES);
    }
    bast_map_free(m);
}

/* A pistol shot at an enemy spawns blood on its near side; at a barrel, a
 * puff. */
static void test_pistol_enemy(void)
{
    bast_map *m = open_map();
    game_state gs;
    setup(&gs, m);
    assert(thinker_spawn(&gs.things, BAST_ENT_MUTT_TROOPER, FX(6) + HALF, FX(3) + HALF, 0));
    game_tic(&gs, &FIRE);
    assert(fx_count(&gs) == 1);
    const fx_t *f = &gs.fx.list[0];
    assert(f->kind == FX_BLOOD);
    assert(fix_to_int(f->x) == 6 && fix_to_int(f->y) == 3);
    assert(f->x < FX(6) + HALF);

    setup(&gs, m);
    assert(thinker_spawn_barrel(&gs.things, FX(6) + HALF, FX(3) + HALF));
    game_tic(&gs, &FIRE);
    assert(fx_count(&gs) == 1 && gs.fx.list[0].kind == FX_PUFF);
    bast_map_free(m);
}

/* Scarab in flight and its explosion are listed with the fx sprites. */
static void test_render_projectile(void)
{
    bast_map *m = open_map();
    game_state gs;
    setup(&gs, m);
    assert(projectile_spawn(&gs, TH_TYPE_SCARAB_PROJ, TH_ID_PLAYER, gs.player.x, gs.player.y,
                            PLAYER_EYE_Z, 0));
    render_thing out[4];
    int frames_seen = 0, boom = 0;
    for (int t = 0; t < 120; t++) {
        game_tic(&gs, &IDLE);
        const int n = fx_render_things(&gs, out, 0, 4);
        if (!n)
            break;
        assert(n == 1);
        if (out[0].type == SPRITE_FX_BASE + FXS_SCARAB_PROJ) {
            assert(!boom && out[0].frame < FX_SCARAB_FRAMES && out[0].z == PLAYER_EYE_Z);
            frames_seen |= 1 << out[0].frame;
        } else {
            assert(out[0].type == SPRITE_FX_BASE + FXS_EXPLOSION_SMALL);
            assert(out[0].frame == boom / FX_TICS_PER_FRAME);
            boom++;
        }
    }
    printf("scarab: frames mask %x, explosion %d tics\n", frames_seen, boom);
    assert(frames_seen == 0xf);
    assert(boom == fx_tics(TH_TYPE_EXPLOSION_SMALL));
    bast_map_free(m);
}

/* Woofu's fan bolt (issue #228) and the Pomeranian's plasma bolt draw the
 * plasma_bolt sprite at their own height, looping; their impact draws
 * plasma_hit, one frame per FX_TICS_PER_FRAME. */
static void test_render_plasma(void)
{
    static const uint16_t types[] = { TH_TYPE_BOSS_BOLT, TH_TYPE_PLASMA_BOLT };
    for (size_t k = 0; k < 2; k++) {
        bast_map *m = open_map();
        game_state gs;
        setup(&gs, m);
        const fix_t z = FX(1) / 2;
        /* Flying east from x 4.5 (ahead of the player) into the wall at x 15. */
        assert(projectile_spawn(&gs, types[k], TH_ID_PLAYER, FX(4) + HALF, FX(3) + HALF, z, 0));
        render_thing out[4];
        int frames_seen = 0, hit = 0;
        for (int t = 0; t < 200; t++) {
            game_tic(&gs, &IDLE);
            const int n = fx_render_things(&gs, out, 0, 4);
            if (!n)
                break;
            assert(n == 1);
            if (out[0].type == SPRITE_FX_BASE + FXS_PLASMA_BOLT) {
                assert(!hit && out[0].frame < FX_PLASMA_BOLT_FRAMES && out[0].z == z);
                frames_seen |= 1 << out[0].frame;
            } else {
                assert(out[0].type == SPRITE_FX_BASE + FXS_PLASMA_HIT);
                assert(out[0].frame == hit / FX_TICS_PER_FRAME);
                hit++;
            }
        }
        printf("type %u: frames mask %x, plasma_hit %d tics\n", types[k], frames_seen, hit);
        assert(frames_seen == 0xf);
        assert(hit == fx_tics(TH_TYPE_PLASMA_HIT));
        bast_map_free(m);
    }
}

/* The effect list is part of the state hash. */
static void test_hash(void)
{
    bast_map *m = open_map();
    game_state a, b;
    setup(&a, m);
    setup(&b, m);
    assert(game_hash(&a) == game_hash(&b));
    fx_spawn(&b, FX_PUFF, FX(5), FX(3));
    assert(game_hash(&a) != game_hash(&b));
    bast_map_free(m);
}

int main(int argc, char **argv)
{
    static const struct {
        const char *name;
        void (*fn)(void);
    } groups[] = {
        { "lifetime", test_lifetime },     { "overflow", test_overflow },
        { "pistol_wall", test_pistol_wall }, { "pistol_enemy", test_pistol_enemy },
        { "render_projectile", test_render_projectile }, { "hash", test_hash },
        { "render_plasma", test_render_plasma },
    };
    for (size_t i = 0; i < sizeof groups / sizeof groups[0]; i++) {
        if (argc < 2 || strcmp(argv[1], groups[i].name) == 0) {
            groups[i].fn();
            if (argc >= 2)
                return 0;
        }
    }
    if (argc >= 2) {
        fprintf(stderr, "test_fx: unknown group %s\n", argv[1]);
        return 2;
    }
    return 0;
}
