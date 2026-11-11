#include "game/input.h"

static int clamp127(int v)
{
    return v > TICCMD_MAX ? TICCMD_MAX : v < -TICCMD_MAX ? -TICCMD_MAX : v;
}

void input_build_ticcmd(const input_state *in, ticcmd *cmd)
{
    int fwd = 0, side = 0, turn = 0;
    if (in->forward)
        fwd += TICCMD_MAX;
    if (in->back)
        fwd -= TICCMD_MAX;
    if (in->strafe_right)
        side += TICCMD_MAX;
    if (in->strafe_left)
        side -= TICCMD_MAX;
    if (in->turn_left)
        turn += PLAYER_TURN_SPEED;
    if (in->turn_right)
        turn -= PLAYER_TURN_SPEED;
    /* mouse right = clockwise; clamp before multiplying to avoid overflow */
    int dx = in->mouse_dx;
    if (dx > 65536)
        dx = 65536;
    if (dx < -65536)
        dx = -65536;
    turn -= dx * INPUT_MOUSE_TURN;
    if (turn > INT16_MAX)
        turn = INT16_MAX;
    if (turn < INT16_MIN)
        turn = INT16_MIN;

    cmd->fwd = (int8_t)clamp127(fwd);
    cmd->side = (int8_t)clamp127(side);
    cmd->turn = (int16_t)turn;
    cmd->buttons = 0;
    if (in->fire)
        cmd->buttons |= BT_FIRE;
    if (in->use)
        cmd->buttons |= BT_USE; /* opens doors; dead, restarts */
    if (in->run)
        cmd->buttons |= BT_RUN;
    if (in->weapon_slot >= 1 && in->weapon_slot <= 7)
        cmd->buttons |= (uint8_t)(in->weapon_slot << BT_WEAPON_SHIFT);
}

int input_weapon_key(int key)
{
    return key >= '1' && key <= '7' ? key - '0' : 0;
}
