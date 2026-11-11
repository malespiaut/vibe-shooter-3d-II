#include "engine/sprite.h"

#include <stddef.h>

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
    [BAST_ENT_TEST_SPRITE] = { .name = "testspr", .frames = 1, .live = 0 },

    [BAST_ENT_HAIRBALL_SCATTERGUN] = { .name = "items/wpn3", .frames = 1, .live = 1 },
    [BAST_ENT_SISTRUM_REPEATER] = { .name = "items/wpn4", .frames = 1, .live = 1 },
    [BAST_ENT_SCARAB_LAUNCHER] = { .name = "items/wpn5", .frames = 1, .live = 1 },
    [BAST_ENT_EYE_OF_RA_LANCE] = { .name = "items/wpn6", .frames = 1, .live = 1 },
    [BAST_ENT_SPHINX_HOWLER] = { .name = "items/wpn7", .frames = 1, .live = 1 },

    [BAST_ENT_FISH_SNACK] = { .name = "items/fish", .frames = 1, .live = 1 },
    [BAST_ENT_TUNA_TIN] = { .name = "items/tuna", .frames = 1, .live = 1 },
    [BAST_ENT_GOLDEN_SARDINE] = { .name = "items/sardine", .frames = 2, .live = 1 },
    [BAST_ENT_LINEN_WRAP] = { .name = "items/linen", .frames = 1, .live = 1 },
    [BAST_ENT_SCARAB_PLATE] = { .name = "items/plate", .frames = 2, .live = 1 },
    [BAST_ENT_BANDAGE_SCRAP] = { .name = "items/bandage", .frames = 1, .live = 1 },
    [BAST_ENT_CHARGE_VIAL] = { .name = "items/vial", .frames = 1, .live = 1 },
    [BAST_ENT_CHARGE_JAR] = { .name = "items/jar", .frames = 1, .live = 1 },
    [BAST_ENT_YARN_BUNDLE] = { .name = "items/yarn", .frames = 1, .live = 1 },
    [BAST_ENT_YARN_BASKET] = { .name = "items/basket", .frames = 1, .live = 1 },
    [BAST_ENT_SCARAB] = { .name = "items/scarab", .frames = 1, .live = 1 },
    [BAST_ENT_SCARAB_CRATE] = { .name = "items/crate_scarab", .frames = 1, .live = 1 },
    [BAST_ENT_SUN_SHARD] = { .name = "items/shard", .frames = 1, .live = 1 },
    [BAST_ENT_SUN_ORB] = { .name = "items/orb", .frames = 2, .live = 1 },
    [BAST_ENT_LITTER_SATCHEL] = { .name = "items/satchel", .frames = 1, .live = 1 },
    [BAST_ENT_GREEN_ANKH] = { .name = "items/key_green", .frames = 2, .live = 1 },
    [BAST_ENT_BLUE_ANKH] = { .name = "items/key_blue", .frames = 2, .live = 1 },
    [BAST_ENT_VIOLET_ANKH] = { .name = "items/key_violet", .frames = 2, .live = 1 },
    [BAST_ENT_CATNIP_FRENZY] = { .name = "items/catnip", .frames = 2, .live = 1 },
    [BAST_ENT_CATS_EYE_AMULET] = { .name = "items/amulet", .frames = 1, .live = 1 },
    [BAST_ENT_NINTH_LIFE] = { .name = "items/ninth", .frames = 1, .live = 1 },

    /* enemies.md sprite names (assets/src/sprites/<name>/, pre-rendered by
     * tools/build.d/60-sprites.sh; gibs by 65-gibs.sh). Sets not built yet
     * are skipped by the renderer with a warning. */
    [BAST_ENT_MUTT_TROOPER] = { .name = "mutt", .frames = 2, .live = 1, .enemy = 1 },
    [BAST_ENT_CHIHUAHUA_CHOMPER] = { .name = "chomp", .frames = 2, .live = 1, .enemy = 1 },
    [BAST_ENT_POMERANIAN_PLASMAGUNNER] = { .name = "pom", .frames = 2, .live = 1, .enemy = 1 },
    [BAST_ENT_DACHSHUND_HOWITZER] = { .name = "dach", .frames = 2, .live = 1, .enemy = 1 },
    [BAST_ENT_MASTIFF_JUGGERNAUT] = { .name = "mastiff", .frames = 2, .live = 1, .enemy = 1 },
    [BAST_ENT_SHEPHERD_SHIELDBEARER] = { .name = "shep", .frames = 2, .live = 1, .enemy = 1 },
    /* enemies.md Animation list: the boss has no `gib`. */
    [BAST_ENT_WOOFU] = { .name = "woofu", .frames = 2, .live = 1, .enemy = 1, .no_gib = 1 },
};

static const char *const anim_names[SPR_ANIMS] = {
    "idle", "walk", "windup", "attack", "pain", "death", "gib",
};
static const int enemy_frames[SPR_ANIMS] = { 2, 4, 2, 2, 1, 5, 8 };

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
