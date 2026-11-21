/* Level start fairness (issue #231, levels.md section 1): at every skill,
 * no enemy but the one allowed "first contact" cell may see the player
 * start at tic 0 (ai_line_of_sight) or be woken by the first shot fired
 * there (ai_noise). Otherwise an off-screen enemy winds up and hits the
 * player before any enemy is on screen.
 *
 *   test_level_start <map.vmap> <allowed tx> <allowed ty>
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "bast.h"
#include "game/ai.h"
#include "game/enemy_info.h"
#include "game/state.h"

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: test_level_start <map.vmap> <tx> <ty>\n");
        return 2;
    }
    const int ax = atoi(argv[2]), ay = atoi(argv[3]);
    bast_map *m = bast_map_load(argv[1]);
    if (!m) {
        fprintf(stderr, "test_level_start: %s: %s\n", argv[1], bast_error());
        return 1;
    }
    static game_state gs;
    int bad = 0;
    for (int skill = 1; skill <= 3; skill++) {
        game_init(&gs, m, 1, skill, 0);
        ai_noise(&gs); /* the player's first shot, from the start */
        int enemies = 0;
        for (int i = 0; i < gs.things.count; i++) {
            const thinker_t *t = &gs.things.list[i];
            if (!thinker_alive(t) || !enemy_info(t->type))
                continue;
            enemies++;
            const int tx = fix_to_int(t->x), ty = fix_to_int(t->y);
            if (tx == ax && ty == ay)
                continue;
            const int sees = ai_line_of_sight(&gs, t);
            const int woke = t->state != TH_STATE_STAND;
            if (sees || woke) {
                fprintf(stderr, "skill %d: enemy at (%d,%d)%s%s\n", skill, tx, ty,
                        sees ? " sees the start" : "", woke ? " hears the first shot" : "");
                bad++;
            }
        }
        printf("skill %d: %d enemies, start clear except (%d,%d)\n", skill, enemies, ax, ay);
    }
    bast_map_free(m);
    assert(bad == 0);
    return 0;
}
