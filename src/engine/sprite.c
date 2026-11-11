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
};

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
