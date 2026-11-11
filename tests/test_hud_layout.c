/* hud_layout (issue #99): draws the HUD for a fixed state and checks the
 * hud.md boxes: each right-aligned number ends exactly on its box's right
 * edge (the `%` on x=103, ...), icons, keys, powerup bar, border flash and
 * a pickup message.
 *   test_hud_layout <assets/gen> <catlantis.gpl> [out.png] */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bast.h"
#include "engine/colormap.h"
#include "engine/draw2d.h"
#include "engine/png.h"
#include "engine/video.h"
#include "game/door.h"
#include "game/hud.h"
#include "game/items.h"

static uint8_t fb[SCREEN_W * SCREEN_H], ref[SCREEN_W * SCREEN_H];
static game_state gs;
static hud_state hs;

#define PX(b, x, y) ((b)[(y) * SCREEN_W + (x)])

/* Rightmost column in (x0..x1, y0..y1) where fb differs from ref, -1 if
 * none. */
static int right_edge(int x0, int y0, int x1, int y1)
{
    for (int x = x1; x >= x0; x--)
        for (int y = y0; y <= y1; y++)
            if (PX(fb, x, y) != PX(ref, x, y))
                return x;
    return -1;
}

static bast_tex *tex(const char *dir, const char *name)
{
    char p[512];
    snprintf(p, sizeof p, "%s/hud/%s.tex", dir, name);
    bast_tex *t = bast_tex_load(p);
    assert(t);
    return t;
}

/* Opaque pixels of `t` at (x, y) are on screen as-is. */
static void check_patch(const bast_tex *t, int x, int y)
{
    for (int i = 0; i < t->width; i++)
        for (int j = 0; j < t->height; j++) {
            uint8_t c = t->pixels[i * t->height + j];
            if (c != 255)
                assert(PX(fb, x + i, y + j) == c);
        }
}

/* Entry `idx` of the .gpl file is named `name`. */
static void check_gpl_name(const char *gpl, int idx, const char *name)
{
    FILE *f = fopen(gpl, "r");
    assert(f);
    char line[256];
    int i = -1, ok = 0;
    while (fgets(line, sizeof line, f)) {
        int r, g, b;
        char n[64];
        if (sscanf(line, "%d %d %d %63s", &r, &g, &b, n) == 4 && ++i == idx) {
            ok = strcmp(n, name) == 0;
            break;
        }
    }
    fclose(f);
    assert(ok);
}

int main(int argc, char **argv)
{
    assert(argc >= 3);
    const char *dir = argv[1];
    check_gpl_name(argv[2], HUD_IDX_FB_GOLD, "fb_gold");
    int first, count;
    assert(ramp_find("red", &first, &count));
    const uint8_t red_12 = (uint8_t)(first + 12);
    assert(ramp_find("gold", &first, &count));
    const uint8_t gold_00 = (uint8_t)first;
    assert(colormap_load(dir));
    assert(hud_init(dir));

    /* Fixed state: health 100, armour 37, pistol ready, ammo 50/12/4/160,
     * green + violet keys, Catnip half gone, 3 owned slots. */
    memset(&gs, 0, sizeof gs);
    player_t *p = &gs.player;
    p->health = 100;
    p->armour = 37;
    p->keys = KEY_GREEN | KEY_VIOLET;
    p->catnip_tics = 525;
    p->weapons.owned = (1 << WP_CLAWS) | (1 << WP_PISTOL) | (1 << WP_SCATTERGUN);
    p->weapons.ready = WP_PISTOL;
    p->weapons.ammo[AM_CHARGES] = 50;
    p->weapons.ammo[AM_SHELLS] = 12;
    p->weapons.ammo[AM_SCARABS] = 4;
    p->weapons.ammo[AM_CELLS] = 160;
    hud_state_init(&hs, &gs, 0);

    /* Reference: the bare panel. */
    memset(ref, 0, sizeof ref);
    bast_tex *panel = tex(dir, "statusbar");
    assert(panel->width == 320 && panel->height == 32);
    draw_patch(ref, panel, 0, HUD_BAR_Y);

    memset(fb, 0, sizeof fb);
    hud_draw(fb, &hs, &gs);

    /* Right-aligned numbers end on the box edge. */
    assert(right_edge(56, 210, 111, 225) == 103); /* "100%" */
    assert(right_edge(56, 226, 111, 234) == 103); /* armour "37" */
    assert(right_edge(180, 210, 229, 225) == 227); /* current ammo "50" */
    for (int a = 0; a < AM_TYPES; a++)            /* ammo table rows */
        assert(right_edge(290, 210 + 7 * a, 319, 210 + 7 * a + 6) == 319);
    /* The `%` glyph's shadow column is x=103 (gold_00, hud.md Fonts). */
    int shadow = 0;
    for (int y = 210; y <= 225; y++)
        shadow |= PX(fb, 103, y) == gold_00;
    assert(shadow);
    /* Health number starts left of the % and right of the fish icon. */
    assert(right_edge(53, 210, 55, 225) == -1);

    /* Icons at their hud.md positions. */
    check_patch(tex(dir, "icon_fish"), 40, 212);
    check_patch(tex(dir, "icon_scarab"), 40, 226);
    check_patch(tex(dir, "weapon_2"), 120, 210);
    check_patch(tex(dir, "key_green"), 232, 212);
    check_patch(tex(dir, "key_socket"), 248, 212);
    check_patch(tex(dir, "key_violet"), 264, 212);
    check_patch(tex(dir, "ammo_charge"), 280, 210);
    check_patch(tex(dir, "ammo_sun"), 280, 231);
    /* Face: tier 1 idle exists on main (#105) and fills (4,208)-(35,239). */
    char fpath[64];
    snprintf(fpath, sizeof fpath, "faces/face_t1_idle0");
    check_patch(tex(dir, fpath), 4, 208);

    /* Slot row: current (2) gold, owned (1, 3) bright gray, missing dark. */
    int gfirst, gcount, yfirst, ycount;
    assert(ramp_find("gold", &gfirst, &gcount) && ramp_find("gray", &yfirst, &ycount));
    int cur_gold = 0, missing_bright = 0, owned_bright = 0;
    for (int x = 128; x < 134; x++)
        for (int y = 231; y < 239; y++)
            cur_gold |= PX(fb, x, y) >= gfirst + 2 && PX(fb, x, y) < gfirst + gcount;
    for (int x = 122; x < 128; x++)
        for (int y = 231; y < 239; y++)
            owned_bright |= PX(fb, x, y) >= yfirst + 13 && PX(fb, x, y) < yfirst + ycount;
    for (int x = 140; x < 164; x++)
        for (int y = 231; y < 239; y++)
            missing_bright |= PX(fb, x, y) >= yfirst + 13 && PX(fb, x, y) < yfirst + ycount;
    assert(cur_gold && owned_bright && !missing_bright);

    /* Powerup bar: 525/1050 -> 8 of 16 px, fullbright gold. */
    for (int x = 38; x <= 45; x++)
        assert(PX(fb, x, 236) == HUD_IDX_FB_GOLD && PX(fb, x, 237) == HUD_IDX_FB_GOLD);
    assert(PX(fb, 46, 236) != HUD_IDX_FB_GOLD);

    /* Claws: current ammo blank. */
    p->weapons.ready = WP_CLAWS;
    memset(fb, 0, sizeof fb);
    hud_draw(fb, &hs, &gs);
    assert(right_edge(180, 210, 229, 225) == -1);
    p->weapons.ready = WP_PISTOL;

    /* A hit: view border red_12 for 4 tics (top 2 rows, 2 side columns). */
    player_damage(&gs, 5);
    for (int t = 0; t < HUD_FLASH_HIT_TICS; t++) {
        hud_tic(&hs, &gs);
        memset(fb, 0, sizeof fb);
        hud_draw(fb, &hs, &gs);
        assert(PX(fb, 160, 0) == red_12 && PX(fb, 160, 1) == red_12 && PX(fb, 160, 2) == 0);
        assert(PX(fb, 1, 100) == red_12 && PX(fb, 318, 207) == red_12);
        assert(PX(fb, 2, 100) == 0 && PX(fb, 317, 100) == 0);
    }
    hud_tic(&hs, &gs);
    memset(fb, 0, sizeof fb);
    hud_draw(fb, &hs, &gs);
    assert(PX(fb, 160, 0) == 0);
    assert(hs.face.expr == FACE_HURTF); /* no source: front */

    /* A pickup: message at (4,2) for 105 tics, gold_10 border for 3. */
    game_event ev = { 0 };
    ev.kind = GEV_PICKUP;
    ev.type = BAST_ENT_FISH_SNACK;
    game_post_event(&gs, &ev);
    hud_tic(&hs, &gs);
    assert(strcmp(hs.msg[0].text, "PICKED UP A FISH SNACK.") == 0);
    memset(fb, 0, sizeof fb);
    hud_draw(fb, &hs, &gs);
    assert(PX(fb, 160, 0) == (uint8_t)(gfirst + 10));
    int msg = 0;
    for (int x = 4; x < 10; x++)
        for (int y = 2; y < 10; y++)
            msg |= PX(fb, x, y) != 0;
    assert(msg);
    if (argc > 3)
        assert(png_write_indexed(argv[3], fb, SCREEN_W, SCREEN_H, colormap_palette()));
    for (int t = 1; t < HUD_MSG_TICS; t++)
        hud_tic(&hs, &gs);
    assert(hs.msg[0].tics == 1);
    hud_tic(&hs, &gs);
    assert(hs.msg[0].tics == 0);

    /* Three messages: max 2 lines, the oldest scrolls off. */
    for (int i = 0; i < 3; i++) {
        ev.type = (uint16_t)(BAST_ENT_FISH_SNACK + i);
        game_post_event(&gs, &ev);
        hud_tic(&hs, &gs);
    }
    assert(strcmp(hs.msg[0].text, "PICKED UP A TUNA TIN.") == 0);
    assert(strcmp(hs.msg[1].text, "PICKED UP A GOLDEN SARDINE.") == 0);

    bast_tex_free(panel);
    hud_shutdown();
    printf("hud_layout: ok\n");
    return 0;
}
