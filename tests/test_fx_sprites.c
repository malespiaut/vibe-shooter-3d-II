/* fx_sprites (issue #147): every frame of every effect sprite FXS_* exists
 * in the built assets under the path the renderer asks for, and decodes.
 * A wrong name in sprite.c's fx table (e.g. "blood_hit" for the built
 * blood_hit_hit_d0_f*.tex) makes the renderer skip that effect, so this
 * fails instead. Reads through files.c: game.pak first, then loose files.
 * Needs tools/build_assets.sh (tools/build.d/40-fx.sh).
 *
 *   test_fx_sprites <assets/gen dir> */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "bast.h"
#include "engine/files.h"
#include "engine/sprite.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_fx_sprites <assets/gen dir>\n");
        return 2;
    }
    if (!files_init(argv[1])) {
        fprintf(stderr, "test_fx_sprites: %s\n", files_error());
        return 1;
    }
    int bad = 0;
    for (int i = 0; i < FXS_COUNT; i++) {
        const fx_sprite_def *d = fx_sprite_def_for(i);
        assert(d && d->frames >= 1 && d->frames <= FX_SPRITE_MAX_FRAMES);
        for (int f = 0; f < d->frames; f++) {
            char path[256];
            assert(fx_sprite_path(i, f, path, sizeof path));
            int64_t n = 0;
            uint8_t *data = files_read(path, &n);
            const int found = data != NULL;
            bast_tex *t = found ? bast_tex_from_memory(data, n) : NULL;
            free(data);
            if (!t) {
                fprintf(stderr, "FAIL: FXS %d frame %d: %s %s\n", i, f, path,
                        found ? "does not decode" : "missing");
                bad++;
                continue;
            }
            if (f == 0)
                printf("ok: FXS %d %s (%d frames)\n", i, path, d->frames);
            bast_tex_free(t);
        }
    }
    /* Out-of-range ids and frames give no path. */
    char buf[64];
    assert(!fx_sprite_path(-1, 0, buf, sizeof buf));
    assert(!fx_sprite_path(FXS_COUNT, 0, buf, sizeof buf));
    assert(!fx_sprite_path(FXS_PUFF, fx_sprite_def_for(FXS_PUFF)->frames, buf, sizeof buf));
    assert(!fx_sprite_path(FXS_BLOOD, 0, buf, 8));
    files_shutdown();
    return bad ? 1 : 0;
}
