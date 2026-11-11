#include "engine/sprite.h"

#include <stddef.h>

#include "bast.h"

int sprite_dir(angle_t viewer_angle, angle_t thing_angle)
{
    /* Offset by half a sector so d0 is centred on "facing the camera". */
    angle_t rel = (angle_t)(viewer_angle - thing_angle + ANG45 / 2);
    return rel >> 13; /* 65536 / 8 = 8192 = 1 << 13 */
}

const char *sprite_name_for_entity(int type)
{
    switch (type) {
    case BAST_ENT_TEST_SPRITE:
        return "testspr";
    default:
        return NULL;
    }
}
