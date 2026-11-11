/* sprite_dir (issue #15): 8-direction frame selection per tech.md "Sprite
 * naming": d0 = the thing faces the camera, then the camera moves
 * counter-clockwise around it in 45 degree steps. */
#include <assert.h>
#include <stdio.h>

#include "bast.h"
#include "engine/sprite.h"

int main(void)
{
    /* Thing faces east (0). Camera due east of it: it faces the camera. */
    assert(sprite_dir(0, 0) == 0);
    /* Camera moves counter-clockwise: north-east, north, ... */
    for (int k = 0; k < 8; k++) {
        angle_t va = (angle_t)(k * ANG45);
        assert(sprite_dir(va, 0) == k);
        /* Same answer for any facing: only the difference matters. */
        for (int f = 0; f < 8; f++) {
            angle_t ta = (angle_t)(f * ANG45 + 1234);
            assert(sprite_dir((angle_t)(va + ta), ta) == k);
        }
    }
    /* Camera behind the thing: d4 (back). Camera on its left (CCW 90): d2. */
    assert(sprite_dir(ANG180, 0) == 4);
    assert(sprite_dir(ANG270, ANG180) == 2);  /* faces west, camera south */
    assert(sprite_dir(ANG90, ANG180) == 6);   /* faces west, camera north */

    /* Boundaries: each sector is [k*45 - 22.5, k*45 + 22.5). */
    const angle_t half = ANG45 / 2; /* 4096 */
    for (int k = 0; k < 8; k++) {
        angle_t c = (angle_t)(k * ANG45);
        assert(sprite_dir((angle_t)(c + half - 1), 0) == k);
        assert(sprite_dir((angle_t)(c + half), 0) == (k + 1) % 8);
        assert(sprite_dir((angle_t)(c - half), 0) == k);
        assert(sprite_dir((angle_t)(c - half - 1), 0) == (k + 7) % 8);
    }
    /* Wraparound at 0 / 65535. */
    assert(sprite_dir(65535, 0) == 0);
    assert(sprite_dir(0, 1) == 0);
    assert(sprite_dir((angle_t)(65536 - half - 1), 0) == 7);

    /* Every value in range for every angle. */
    for (long a = 0; a < 65536; a += 7)
        assert(sprite_dir((angle_t)a, 12345) >= 0 && sprite_dir((angle_t)a, 12345) < 8);

    assert(sprite_name_for_entity(BAST_ENT_TEST_SPRITE) != NULL);
    assert(sprite_name_for_entity(BAST_ENT_PLAYER_START) == NULL);
    printf("sprite_dir: ok\n");
    return 0;
}
