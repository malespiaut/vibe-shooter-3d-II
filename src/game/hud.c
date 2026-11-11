/* In-game HUD (hud.md "Status bar", "Other HUD elements"). */
#include "game/hud.h"

#include <stdio.h>
#include <string.h>

#include "bast.h"
#include "engine/draw2d.h"
#include "engine/render.h"
#include "engine/video.h"
#include "game/door.h"
#include "game/weapon.h"

/* Face TEX names the art may provide: 5 tiers x 10 expressions + 2. */
#define FACE_SLOTS (5 * FACE_EXPR_COUNT + 2)

static struct {
    int loaded;
    font *small, *big;
    bast_tex *panel, *fish, *scarab, *socket;
    bast_tex *key[3];     /* green, blue, violet */
    bast_tex *ammo[AM_TYPES];
    bast_tex *weapon[WP_SLOTS];
    char face_name[FACE_SLOTS][24];
    bast_tex *face[FACE_SLOTS];
    uint8_t gold[256], dark[256];
    uint8_t red_12, gold_10;
} H;

static char err[1024];

const char *hud_error(void)
{
    return err;
}

static bast_tex *load(const char *dir, const char *name, int required)
{
    char path[512];
    snprintf(path, sizeof path, "%s/hud/%s.tex", dir, name);
    bast_tex *t = bast_tex_load(path);
    if (!t && required)
        snprintf(err, sizeof err, "%s: %s", path, bast_error());
    return t;
}

static int ramp_entry(const char *ramp, int k)
{
    int first = 0, count = 0;
    ramp_find(ramp, &first, &count);
    return first + k;
}

int hud_init(const char *dir)
{
    hud_shutdown();
    static const char *const keys[3] = { "key_green", "key_blue", "key_violet" };
    static const char *const ammo[AM_TYPES] = { "ammo_charge", "ammo_yarn", "ammo_scarab",
                                                "ammo_sun" };
    H.small = font_load(dir, "small");
    H.big = font_load(dir, "bigdigits");
    if (!H.small || !H.big) {
        snprintf(err, sizeof err, "%s/fonts: cannot load small/bigdigits", dir);
        goto fail;
    }
    if (!(H.panel = load(dir, "statusbar", 1)) || !(H.fish = load(dir, "icon_fish", 1)) ||
        !(H.scarab = load(dir, "icon_scarab", 1)) || !(H.socket = load(dir, "key_socket", 1)))
        goto fail;
    for (int i = 0; i < 3; i++)
        if (!(H.key[i] = load(dir, keys[i], 1)))
            goto fail;
    for (int i = 0; i < AM_TYPES; i++)
        if (!(H.ammo[i] = load(dir, ammo[i], 1)))
            goto fail;
    for (int i = 1; i < WP_SLOTS; i++) {
        char n[16];
        snprintf(n, sizeof n, "weapon_%d", i);
        if (!(H.weapon[i] = load(dir, n, 1)))
            goto fail;
    }
    /* Faces: whatever art exists (#88/#89 add tiers as they land). */
    static const char *const ex[FACE_EXPR_COUNT] = { "idle0", "idle1", "idle2", "hurtl",
                                                     "hurtr", "hurtf", "bighit", "fury",
                                                     "smug", "frenzy", "dead", "god" };
    int n = 0;
    for (int t = 1; t <= 5; t++)
        for (int e = 0; e < FACE_EXPR_COUNT; e++)
            if (e != FACE_DEAD && e != FACE_GOD)
                snprintf(H.face_name[n++], sizeof H.face_name[0], "face_t%d_%s", t, ex[e]);
    snprintf(H.face_name[n++], sizeof H.face_name[0], "face_dead");
    snprintf(H.face_name[n++], sizeof H.face_name[0], "face_god");
    for (int i = 0; i < n; i++) {
        char sub[40];
        snprintf(sub, sizeof sub, "faces/%s", H.face_name[i]);
        H.face[i] = load(dir, sub, 0);
    }

    /* Small font (gray 13-15, shadow gray_00): current slot in gold, missing
     * slots dark (gray_13..15 -> gray_05..07, shadow kept). */
    remap_ramp(H.gold, "gray", "gold");
    remap_identity(H.dark);
    for (int k = 1; k < 16; k++)
        H.dark[ramp_entry("gray", k)] = (uint8_t)ramp_entry("gray", k > 8 ? k - 8 : 1);
    H.red_12 = (uint8_t)ramp_entry("red", 12);
    H.gold_10 = (uint8_t)ramp_entry("gold", 10);
    H.loaded = 1;
    return 1;
fail:
    hud_shutdown();
    return 0;
}

void hud_shutdown(void)
{
    font_free(H.small);
    font_free(H.big);
    bast_tex_free(H.panel);
    bast_tex_free(H.fish);
    bast_tex_free(H.scarab);
    bast_tex_free(H.socket);
    for (int i = 0; i < 3; i++)
        bast_tex_free(H.key[i]);
    for (int i = 0; i < AM_TYPES; i++)
        bast_tex_free(H.ammo[i]);
    for (int i = 0; i < WP_SLOTS; i++)
        bast_tex_free(H.weapon[i]);
    for (int i = 0; i < FACE_SLOTS; i++)
        bast_tex_free(H.face[i]);
    memset(&H, 0, sizeof H);
}

const char *hud_pickup_message(int type)
{
    switch (type) {
    case BAST_ENT_ANKH_PISTOL: return "PICKED UP AN ANKH PISTOL.";
    case BAST_ENT_HAIRBALL_SCATTERGUN: return "PICKED UP A HAIRBALL SCATTERGUN.";
    case BAST_ENT_SISTRUM_REPEATER: return "PICKED UP A SISTRUM REPEATER.";
    case BAST_ENT_SCARAB_LAUNCHER: return "PICKED UP A SCARAB LAUNCHER.";
    case BAST_ENT_EYE_OF_RA_LANCE: return "PICKED UP THE EYE OF RA LANCE.";
    case BAST_ENT_SPHINX_HOWLER: return "PICKED UP THE SPHINX HOWLER.";
    case BAST_ENT_FISH_SNACK: return "PICKED UP A FISH SNACK.";
    case BAST_ENT_TUNA_TIN: return "PICKED UP A TUNA TIN.";
    case BAST_ENT_GOLDEN_SARDINE: return "PICKED UP A GOLDEN SARDINE.";
    case BAST_ENT_LINEN_WRAP: return "PICKED UP A LINEN WRAP.";
    case BAST_ENT_SCARAB_PLATE: return "PICKED UP A SCARAB PLATE.";
    case BAST_ENT_BANDAGE_SCRAP: return "PICKED UP A BANDAGE SCRAP.";
    case BAST_ENT_CHARGE_VIAL: return "PICKED UP A CHARGE VIAL.";
    case BAST_ENT_CHARGE_JAR: return "PICKED UP A CHARGE JAR.";
    case BAST_ENT_YARN_BUNDLE: return "PICKED UP A YARN BUNDLE.";
    case BAST_ENT_YARN_BASKET: return "PICKED UP A YARN BASKET.";
    case BAST_ENT_SCARAB: return "PICKED UP A SCARAB.";
    case BAST_ENT_SCARAB_CRATE: return "PICKED UP A SCARAB CRATE.";
    case BAST_ENT_SUN_SHARD: return "PICKED UP A SUN SHARD.";
    case BAST_ENT_SUN_ORB: return "PICKED UP A SUN ORB.";
    case BAST_ENT_LITTER_SATCHEL: return "PICKED UP A LITTER SATCHEL.";
    case BAST_ENT_GREEN_ANKH: return "PICKED UP THE GREEN ANKH.";
    case BAST_ENT_BLUE_ANKH: return "PICKED UP THE BLUE ANKH.";
    case BAST_ENT_VIOLET_ANKH: return "PICKED UP THE VIOLET ANKH.";
    case BAST_ENT_CATNIP_FRENZY: return "PICKED UP CATNIP FRENZY.";
    case BAST_ENT_CATS_EYE_AMULET: return "PICKED UP THE CAT'S EYE AMULET.";
    case BAST_ENT_NINTH_LIFE: return "PICKED UP A NINTH LIFE.";
    default: return NULL;
    }
}

void hud_state_init(hud_state *h, const game_state *gs, int god)
{
    memset(h, 0, sizeof *h);
    hud_face_init(&h->face, 0x48554446u ^ gs->tic);
    h->seen_seq = gs->event_seq;
    h->god = god;
}

static void push_message(hud_state *h, const char *text)
{
    if (h->msg[HUD_MSG_LINES - 1].tics > 0) /* full: the older line scrolls off */
        memmove(&h->msg[0], &h->msg[1], sizeof h->msg[0] * (HUD_MSG_LINES - 1));
    int i = 0;
    while (i < HUD_MSG_LINES - 1 && h->msg[i].tics > 0)
        i++;
    snprintf(h->msg[i].text, sizeof h->msg[i].text, "%s", text);
    h->msg[i].tics = HUD_MSG_TICS;
}

void hud_tic(hud_state *h, const game_state *gs)
{
    const player_t *p = &gs->player;
    if (h->flash_hit > 0)
        h->flash_hit--;
    if (h->flash_pickup > 0)
        h->flash_pickup--;
    for (int i = 0; i < HUD_MSG_LINES; i++)
        if (h->msg[i].tics > 0)
            h->msg[i].tics--;
    /* Keep live lines at the top. */
    if (h->msg[0].tics == 0 && h->msg[1].tics > 0) {
        h->msg[0] = h->msg[1];
        h->msg[1].tics = 0;
    }

    hud_face_input in;
    memset(&in, 0, sizeof in);
    in.health = p->health;
    in.dead = p->dead;
    in.god = h->god;
    in.frenzy = p->catnip_tics > 0;
    in.fire = (gs->prev_buttons & BT_FIRE) != 0;

    uint32_t seq = h->seen_seq;
    if (gs->event_seq - seq > GAME_EVENT_RING) /* more than the ring holds */
        seq = gs->event_seq - GAME_EVENT_RING;
    for (; seq != gs->event_seq; seq++) {
        const game_event *ev = &gs->events[seq % GAME_EVENT_RING];
        if (ev->kind == GEV_PICKUP) {
            h->flash_pickup = HUD_FLASH_PICKUP_TICS;
            const char *m = hud_pickup_message(ev->type);
            if (m)
                push_message(h, m);
            if (ev->type >= BAST_ENT_ANKH_PISTOL && ev->type <= BAST_ENT_SPHINX_HOWLER)
                in.smug = 1;
        } else if (ev->kind == GEV_DAMAGE) {
            h->flash_hit = HUD_FLASH_HIT_TICS;
            if (ev->amount >= in.hurt_amount) {
                in.hurt_amount = ev->amount;
                in.hurt = hud_face_hurt_dir(p->angle, p->x, p->y, ev->has_src, ev->src_x,
                                            ev->src_y);
            }
        }
    }
    h->seen_seq = gs->event_seq;
    hud_face_tic(&h->face, &in);
}

static const bast_tex *face_tex(const hud_face *f)
{
    char name[32];
    hud_face_name(f, name, sizeof name);
    for (int i = 0; i < FACE_SLOTS; i++)
        if (H.face[i] && strcmp(H.face_name[i], name) == 0)
            return H.face[i];
    return NULL;
}

static void draw_bar(uint8_t *fb, const hud_state *h, const game_state *gs)
{
    const player_t *p = &gs->player;
    const weapons_t *w = &p->weapons;
    const int y0 = HUD_BAR_Y;
    char s[16];

    draw_patch(fb, H.panel, 0, y0);

    /* Face (4,208)-(35,239); no TEX for this face: the empty cartouche. */
    const bast_tex *face = face_tex(&h->face);
    if (face)
        draw_patch(fb, face, 4, 208);

    /* Powerup bar (38,236)-(53,237): the timed powerup with the most left. */
    int frac = 0; /* 0..16 px, rounded up */
    if (p->catnip_tics > 0)
        frac = (int)((p->catnip_tics * 16 + 1049) / 1050);
    if (p->catseye_tics > 0) {
        int f2 = (int)((p->catseye_tics * 16 + 2099) / 2100);
        if (f2 > frac)
            frac = f2;
    }
    if (frac > 16)
        frac = 16;
    if (frac > 0)
        draw_fill(fb, 38, 236, 38 + frac - 1, 237, HUD_IDX_FB_GOLD);

    /* Health: fish icon, big digits + % right-aligned to x=103. */
    draw_patch(fb, H.fish, 40, 212);
    snprintf(s, sizeof s, "%d%%", (int)(p->health < 0 ? 0 : p->health));
    draw_text_right(fb, H.big, 103, 210, s, NULL);

    /* Armour: scarab icon, small number right-aligned to x=103. */
    draw_patch(fb, H.scarab, 40, 226);
    snprintf(s, sizeof s, "%d", (int)p->armour);
    draw_text_right(fb, H.small, 103, 226, s, NULL);

    /* Weapon icon and slot row. */
    const int ready = w->ready;
    if (ready > 0 && ready < WP_SLOTS)
        draw_patch(fb, H.weapon[ready], 120, 210);
    for (int i = 1; i < WP_SLOTS; i++) {
        char c[2] = { (char)('0' + i), 0 };
        const uint8_t *rm = i == ready ? H.gold : (w->owned & (1u << i)) ? NULL : H.dark;
        draw_text(fb, H.small, 122 + (i - 1) * H.small->cell_w, 231, c, rm);
    }

    /* Current ammo, blank for Claws. */
    if (ready > 0 && ready < WP_SLOTS && weapon_info[ready].ammo_type != AM_NONE) {
        snprintf(s, sizeof s, "%d", (int)w->ammo[weapon_info[ready].ammo_type]);
        draw_text_right(fb, H.big, 227, 210, s, NULL);
    }

    /* Keys: green, blue, violet sockets at x=232, 248, 264. */
    static const uint32_t kbit[3] = { KEY_GREEN, KEY_BLUE, KEY_VIOLET };
    for (int i = 0; i < 3; i++)
        draw_patch(fb, (p->keys & kbit[i]) ? H.key[i] : H.socket, 232 + 16 * i, 212);

    /* Ammo table: icon + count right-aligned to x=319. */
    for (int a = 0; a < AM_TYPES; a++) {
        const int y = 210 + 7 * a;
        draw_patch(fb, H.ammo[a], 280, y);
        snprintf(s, sizeof s, "%d", (int)w->ammo[a]);
        draw_text_right(fb, H.small, 319, y, s, NULL);
    }
}

void hud_draw(uint8_t *fb, const hud_state *h, const game_state *gs)
{
    if (!H.loaded)
        return;
    /* Border flash: top 2 rows and the 2 side columns of the view. */
    if (h->flash_hit > 0 || h->flash_pickup > 0) {
        const uint8_t c = h->flash_hit > 0 ? H.red_12 : H.gold_10;
        draw_fill(fb, 0, 0, VIEW_W - 1, HUD_FLASH_W - 1, c);
        draw_fill(fb, 0, 0, HUD_FLASH_W - 1, VIEW_H - 1, c);
        draw_fill(fb, VIEW_W - HUD_FLASH_W, 0, VIEW_W - 1, VIEW_H - 1, c);
    }
    for (int i = 0; i < HUD_MSG_LINES; i++)
        if (h->msg[i].tics > 0)
            draw_text(fb, H.small, 4, 2 + i * H.small->cell_h, h->msg[i].text, NULL);
    draw_bar(fb, h, gs);
}
