/* hud_face (issue #99): tiers, hurt direction, big hit, fury, smug, dead,
 * idle glances from the render RNG. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "game/hud_face.h"

#define U(n) ((fix_t)((n) * 65536))

static hud_face_input base(void)
{
    hud_face_input in;
    memset(&in, 0, sizeof in);
    in.health = 100;
    return in;
}

int main(void)
{
    /* Tier boundaries 80/60/40/20/1. */
    assert(hud_face_tier(200) == 1 && hud_face_tier(80) == 1);
    assert(hud_face_tier(79) == 2 && hud_face_tier(60) == 2);
    assert(hud_face_tier(59) == 3 && hud_face_tier(40) == 3);
    assert(hud_face_tier(39) == 4 && hud_face_tier(20) == 4);
    assert(hud_face_tier(19) == 5 && hud_face_tier(1) == 5);
    assert(hud_face_tier(0) == 0 && hud_face_tier(-5) == 0);

    /* Hurt direction: player at origin facing east (angle 0). */
    assert(hud_face_hurt_dir(0, 0, 0, 1, U(5), 0) == FACE_HURTF);
    assert(hud_face_hurt_dir(0, 0, 0, 1, U(5), U(4)) == FACE_HURTF);   /* 38.7 deg */
    assert(hud_face_hurt_dir(0, 0, 0, 1, 0, U(5)) == FACE_HURTL);      /* north = left */
    assert(hud_face_hurt_dir(0, 0, 0, 1, 0, -U(5)) == FACE_HURTR);     /* south = right */
    assert(hud_face_hurt_dir(0, 0, 0, 1, -U(5), U(1)) == FACE_HURTL);  /* behind-left */
    assert(hud_face_hurt_dir(0, 0, 0, 1, -U(5), -U(1)) == FACE_HURTR); /* behind-right */
    assert(hud_face_hurt_dir(0, 0, 0, 0, 0, U(5)) == FACE_HURTF);      /* no source */
    /* Facing north (ANG90): a source to the west is on the left. */
    assert(hud_face_hurt_dir(ANG90, U(3), U(3), 1, U(1), U(3)) == FACE_HURTL);
    assert(hud_face_hurt_dir(ANG90, U(3), U(3), 1, U(5), U(3)) == FACE_HURTR);
    assert(hud_face_hurt_dir(ANG90, U(3), U(3), 1, U(3), U(9)) == FACE_HURTF);

    hud_face f;
    hud_face_input in = base();
    char name[32];

    /* Idle at tier 1, and tier names. */
    hud_face_init(&f, 1);
    hud_face_tic(&f, &in);
    assert(f.expr == FACE_IDLE0 && f.tier == 1);
    hud_face_name(&f, name, sizeof name);
    assert(strcmp(name, "face_t1_idle0") == 0);
    in.health = 45;
    hud_face_tic(&f, &in);
    hud_face_name(&f, name, sizeof name);
    assert(strcmp(name, "face_t3_idle0") == 0);

    /* Small hit: hurt for FACE_HURT_TICS tics, then idle. */
    hud_face_init(&f, 1);
    in = base();
    in.hurt = FACE_HURTL;
    in.hurt_amount = 19;
    hud_face_tic(&f, &in);
    assert(f.expr == FACE_HURTL);
    in = base();
    for (int i = 1; i < FACE_HURT_TICS; i++) {
        hud_face_tic(&f, &in);
        assert(f.expr == FACE_HURTL);
    }
    hud_face_tic(&f, &in);
    assert(f.expr != FACE_HURTL);

    /* Big hit at 20: 35 tics, and a small hit does not cut it short. */
    hud_face_init(&f, 1);
    in = base();
    in.hurt = FACE_HURTR;
    in.hurt_amount = 20;
    hud_face_tic(&f, &in);
    assert(f.expr == FACE_BIGHIT);
    in.hurt_amount = 3;
    hud_face_tic(&f, &in);
    assert(f.expr == FACE_BIGHIT);
    in = base();
    for (int i = 2; i < FACE_BIGHIT_TICS; i++) {
        hud_face_tic(&f, &in);
        assert(f.expr == FACE_BIGHIT);
    }
    hud_face_tic(&f, &in);
    assert(f.expr != FACE_BIGHIT);

    /* Fury after 70 tics of fire held, not at 69; release resets. */
    hud_face_init(&f, 1);
    in = base();
    in.fire = 1;
    for (int i = 1; i < FACE_FURY_TICS; i++) {
        hud_face_tic(&f, &in);
        assert(f.expr != FACE_FURY);
    }
    hud_face_tic(&f, &in);
    assert(f.expr == FACE_FURY);
    in.fire = 0;
    hud_face_tic(&f, &in);
    assert(f.expr != FACE_FURY && f.fire_held == 0);

    /* Smug on weapon pickup for 35 tics; frenzy replaces idle. */
    hud_face_init(&f, 1);
    in = base();
    in.smug = 1;
    hud_face_tic(&f, &in);
    assert(f.expr == FACE_SMUG);
    in = base();
    in.frenzy = 1;
    for (int i = 1; i < FACE_SMUG_TICS; i++)
        hud_face_tic(&f, &in);
    assert(f.expr == FACE_SMUG);
    hud_face_tic(&f, &in);
    assert(f.expr == FACE_FRENZY);

    /* Dead at 0, over everything. */
    in = base();
    in.health = 0;
    in.hurt = FACE_HURTF;
    in.hurt_amount = 50;
    hud_face_tic(&f, &in);
    assert(f.expr == FACE_DEAD && f.tier == 0);
    hud_face_name(&f, name, sizeof name);
    assert(strcmp(name, "face_dead") == 0);

    /* Idle glances: first one 70..140 tics in, lasts FACE_GLANCE_TICS,
     * same sequence for the same seed. */
    for (uint32_t seed = 1; seed < 40; seed++) {
        hud_face a, b;
        hud_face_init(&a, seed);
        hud_face_init(&b, seed);
        in = base();
        int first = -1, len = 0;
        for (int t = 1; t <= 400; t++) {
            hud_face_tic(&a, &in);
            hud_face_tic(&b, &in);
            assert(a.expr == b.expr);
            if (a.expr != FACE_IDLE0) {
                assert(a.expr == FACE_IDLE1 || a.expr == FACE_IDLE2);
                if (first < 0)
                    first = t;
                if (t - first < 30)
                    len++;
            }
        }
        assert(first >= FACE_GLANCE_MIN && first <= FACE_GLANCE_MAX);
        assert(len == FACE_GLANCE_TICS);
    }
    printf("hud_face: ok\n");
    return 0;
}
