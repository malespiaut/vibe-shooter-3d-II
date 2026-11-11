/* Window-mode input to ticcmd (hud.md Controls): movement, turn, run,
 * use, fire, weapon slots 1-7. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "game/input.h"

static ticcmd build(const input_state *in)
{
    ticcmd c;
    memset(&c, 0xAA, sizeof c);
    input_build_ticcmd(in, &c);
    return c;
}

int main(void)
{
    input_state in = { 0 };
    ticcmd c = build(&in);
    assert(c.fwd == 0 && c.side == 0 && c.turn == 0 && c.buttons == 0);

    /* Movement and turn, unchanged from the old main.c mapping. */
    in.forward = 1;
    in.strafe_right = 1;
    in.turn_left = 1;
    c = build(&in);
    assert(c.fwd == TICCMD_MAX && c.side == TICCMD_MAX && c.turn == PLAYER_TURN_SPEED);
    in.back = 1; /* forward + back cancel */
    c = build(&in);
    assert(c.fwd == 0);
    in = (input_state){ .mouse_dx = 3 };
    c = build(&in);
    assert(c.turn == -3 * INPUT_MOUSE_TURN);
    in.mouse_dx = 100000; /* clamped */
    c = build(&in);
    assert(c.turn == INT16_MIN);

    /* Fire is the same bit the demo `fire` command sets. */
    in = (input_state){ .fire = 1 };
    c = build(&in);
    assert(c.buttons == BT_FIRE);
    in.run = 1;
    in.use = 1;
    c = build(&in);
    assert(c.buttons == (BT_FIRE | BT_RUN | BT_USE));

    /* Weapon slots 1-7 encode as the demo `weapon <n>` command does. */
    for (int n = 1; n <= 7; n++) {
        in = (input_state){ .weapon_slot = n };
        c = build(&in);
        assert(c.buttons == (uint8_t)(n << BT_WEAPON_SHIFT));
        assert(((c.buttons & BT_WEAPON_MASK) >> BT_WEAPON_SHIFT) == n);
    }
    in = (input_state){ .weapon_slot = 3, .fire = 1 };
    c = build(&in);
    assert(c.buttons == (BT_FIRE | (3 << BT_WEAPON_SHIFT)));
    /* Out of range slots are ignored, not wrapped into another slot. */
    in = (input_state){ .weapon_slot = 8 };
    assert(build(&in).buttons == 0);
    in = (input_state){ .weapon_slot = -1 };
    assert(build(&in).buttons == 0);

    /* Key mapping: digits 1-7 select, 0/8/9 and letters do not. */
    for (int k = '1'; k <= '7'; k++)
        assert(input_weapon_key(k) == k - '0');
    assert(input_weapon_key('0') == 0 && input_weapon_key('8') == 0 &&
           input_weapon_key('w') == 0);

    printf("input: ok\n");
    return 0;
}
