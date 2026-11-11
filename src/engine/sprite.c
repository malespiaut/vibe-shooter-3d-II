#include "engine/sprite.h"

#include <stddef.h>
#include <stdio.h>

#include "bast.h"

int sprite_dir(angle_t viewer_angle, angle_t thing_angle)
{
    /* Offset by half a sector so d0 is centred on "facing the camera". */
    angle_t rel = (angle_t)(viewer_angle - thing_angle + ANG45 / 2);
    return rel >> 13; /* 65536 / 8 = 8192 = 1 << 13 */
}

/* items.md names (assets/src/sprites/items/, #106 and #108); frame counts
 * follow the "shimmer" notes there. */
static const sprite_def defs[BAST_ENT_TYPE_LIMIT] = {
    [BAST_ENT_TEST_SPRITE] = { "testspr", 1, 0 },

    [BAST_ENT_HAIRBALL_SCATTERGUN] = { "items/wpn3", 1, 1 },
    [BAST_ENT_SISTRUM_REPEATER] = { "items/wpn4", 1, 1 },
    [BAST_ENT_SCARAB_LAUNCHER] = { "items/wpn5", 1, 1 },
    [BAST_ENT_EYE_OF_RA_LANCE] = { "items/wpn6", 1, 1 },
    [BAST_ENT_SPHINX_HOWLER] = { "items/wpn7", 1, 1 },

    [BAST_ENT_FISH_SNACK] = { "items/fish", 1, 1 },
    [BAST_ENT_TUNA_TIN] = { "items/tuna", 1, 1 },
    [BAST_ENT_GOLDEN_SARDINE] = { "items/sardine", 2, 1 },
    [BAST_ENT_LINEN_WRAP] = { "items/linen", 1, 1 },
    [BAST_ENT_SCARAB_PLATE] = { "items/plate", 2, 1 },
    [BAST_ENT_BANDAGE_SCRAP] = { "items/bandage", 1, 1 },
    [BAST_ENT_CHARGE_VIAL] = { "items/vial", 1, 1 },
    [BAST_ENT_CHARGE_JAR] = { "items/jar", 1, 1 },
    [BAST_ENT_YARN_BUNDLE] = { "items/yarn", 1, 1 },
    [BAST_ENT_YARN_BASKET] = { "items/basket", 1, 1 },
    [BAST_ENT_SCARAB] = { "items/scarab", 1, 1 },
    [BAST_ENT_SCARAB_CRATE] = { "items/crate_scarab", 1, 1 },
    [BAST_ENT_SUN_SHARD] = { "items/shard", 1, 1 },
    [BAST_ENT_SUN_ORB] = { "items/orb", 2, 1 },
    [BAST_ENT_LITTER_SATCHEL] = { "items/satchel", 1, 1 },
    [BAST_ENT_GREEN_ANKH] = { "items/key_green", 2, 1 },
    [BAST_ENT_BLUE_ANKH] = { "items/key_blue", 2, 1 },
    [BAST_ENT_VIOLET_ANKH] = { "items/key_violet", 2, 1 },
    [BAST_ENT_CATNIP_FRENZY] = { "items/catnip", 2, 1 },
    [BAST_ENT_CATS_EYE_AMULET] = { "items/amulet", 1, 1 },
    [BAST_ENT_NINTH_LIFE] = { "items/ninth", 1, 1 },

    /* enemies.md sprite names (assets/src/sprites/<name>/, pre-rendered by
     * tools/build.d/60-sprites.sh; gibs by 65-gibs.sh). Sets not built yet
     * are skipped by the renderer with a warning. */
    [BAST_ENT_MUTT_TROOPER] = { "mutt", 2, 1, 1 },
    [BAST_ENT_CHIHUAHUA_CHOMPER] = { "chomp", 2, 1, 1 },
    [BAST_ENT_POMERANIAN_PLASMAGUNNER] = { "pom", 2, 1, 1 },
    [BAST_ENT_DACHSHUND_HOWITZER] = { "dach", 2, 1, 1 },
    [BAST_ENT_MASTIFF_JUGGERNAUT] = { "mastiff", 2, 1, 1 },
    [BAST_ENT_SHEPHERD_SHIELDBEARER] = { "shep", 2, 1, 1 },
    /* enemies.md Animation list: the boss has no `gib`, adds `phase`;
     * phase 2 is the lidless woofu2 set (assets/src/sprites/woofu2). */
    [BAST_ENT_WOOFU] = { "woofu", 2, 1, 1, 1, 1, "woofu2" },
};

static const char *const anim_names[SPR_ANIMS] = {
    "idle", "walk", "windup", "attack", "pain", "death", "gib", "phase",
};
static const int enemy_frames[SPR_ANIMS] = { 2, 4, 2, 2, 1, 5, 8, 6 };

const char *sprite_anim_name(int anim)
{
    return anim >= 0 && anim < SPR_ANIMS ? anim_names[anim] : NULL;
}

int sprite_enemy_frames(int anim)
{
    return anim >= 0 && anim < SPR_ANIMS ? enemy_frames[anim] : 0;
}

const sprite_def *sprite_def_for_entity(int type)
{
    if (type < 0 || type >= BAST_ENT_TYPE_LIMIT || !defs[type].name)
        return NULL;
    return &defs[type];
}

const char *sprite_name_for_entity(int type)
{
    const sprite_def *d = sprite_def_for_entity(type);
    return d ? d->name : NULL;
}

int sprite_anim_frame(uint32_t tic, int frames)
{
    if (frames <= 1)
        return 0;
    return (int)((tic / SPRITE_SHIMMER_TICS) % (uint32_t)frames);
}

/* The "name", "anim" and "frames" of assets/src/fx/<file>.json, copied
 * as they are (tools/fx.py builds <name>_<anim>_d0_f<n>.tex from them). */
static const fx_sprite_def fx_defs[FXS_COUNT] = {
    [FXS_SCARAB_PROJ] = { "scarab_proj", "fly", 4, 1 },
    [FXS_EXPLOSION_SMALL] = { "explosion_small", "boom", 8, 0 },
    [FXS_EXPLOSION_BIG] = { "explosion_big", "boom", 10, 0 },
    [FXS_PUFF] = { "puff", "hit", 4, 0 },
    [FXS_BLOOD] = { "blood_hit", "hit", 4, 0 },
};

const fx_sprite_def *fx_sprite_def_for(int id)
{
    return id >= 0 && id < FXS_COUNT ? &fx_defs[id] : NULL;
}

int fx_sprite_path(int id, int frame, char *buf, size_t size)
{
    const fx_sprite_def *d = fx_sprite_def_for(id);
    if (!d || frame < 0 || frame >= d->frames || size == 0)
        return 0;
    const int n = snprintf(buf, size, "fx/%s_%s_d0_f%d.tex", d->name, d->anim, frame);
    return n > 0 && (size_t)n < size;
}
