/* ctest `mixer`: gain law, voice stealing, source cut, clipping, pan and the
 * distance table (docs/design/audio.md, "Mixer"). Synthetic sounds, no files. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "engine/mixer.h"

static mixer_sound *make_sound(const char *name, int n, int8_t (*gen)(int))
{
    bast_snd *s = bast_snd_new(n);
    assert(s);
    for (int i = 0; i < n; i++)
        s->samples[i] = gen(i);
    mixer_sound *m = mixer_add_sound(name, s);
    assert(m);
    return m;
}

static int8_t gen_ramp(int i) { return (int8_t)((i * 37) % 256 - 128); }
static int8_t gen_max(int i) { (void)i; return 127; }
static int8_t gen_min(int i) { (void)i; return -128; }
static int8_t gen_one(int i) { (void)i; return 100; }

/* (a) A centred full-volume voice reproduces sample << 8 in both channels. */
static void test_unity(void)
{
    mixer_init(NULL);
    mixer_sound *s = make_sound("ramp", 600, gen_ramp);
    assert(mixer_start(s, 1, MIXER_VOL_MAX, MIXER_PAN_CENTER));
    static int16_t out[700 * 2];
    mixer_render(out, 700);
    for (int i = 0; i < 600; i++) {
        int16_t want = (int16_t)(gen_ramp(i) * 256);
        assert(out[2 * i] == want && out[2 * i + 1] == want);
    }
    for (int i = 600; i < 700; i++) /* finished: silence, voice freed */
        assert(out[2 * i] == 0 && out[2 * i + 1] == 0);
    mixer_voice_info vi;
    mixer_get_voice(0, &vi);
    assert(!vi.active);

    /* Documented gain: vol 32 halves (sample*256*32*64 >> 12). */
    assert(mixer_start(s, 1, 32, MIXER_PAN_CENTER));
    mixer_render(out, 10);
    for (int i = 0; i < 10; i++)
        assert(out[2 * i] == (gen_ramp(i) * 256 * 32 * 64) >> 12);
    mixer_shutdown();
}

static int active_count(void)
{
    int n = 0;
    for (int i = 0; i < MIXER_VOICES; i++) {
        mixer_voice_info v;
        mixer_get_voice(i, &v);
        n += v.active;
    }
    return n;
}

/* (b) The 17th start steals exactly one voice: the quietest (oldest on ties). */
static void test_steal(void)
{
    mixer_init(NULL);
    mixer_sound *s = make_sound("long", 22050, gen_one);
    mixer_sound *t = make_sound("other", 22050, gen_one);
    int16_t out[2];
    for (int i = 0; i < MIXER_VOICES; i++)
        assert(mixer_start(s, 100 + i, i == 9 ? 10 : 50, 0));
    mixer_render(out, 1);
    assert(active_count() == MIXER_VOICES);
    mixer_voice_info before[MIXER_VOICES];
    for (int i = 0; i < MIXER_VOICES; i++)
        mixer_get_voice(i, &before[i]);

    assert(mixer_start(t, 999, 40, 0));
    mixer_render(out, 1);
    assert(active_count() == MIXER_VOICES);
    int changed = 0;
    for (int i = 0; i < MIXER_VOICES; i++) {
        mixer_voice_info v;
        mixer_get_voice(i, &v);
        if (v.serial != before[i].serial) {
            changed++;
            assert(before[i].source_id == 109 && before[i].vol == 10);
            assert(v.source_id == 999 && v.sound == t && v.pos == 1);
        }
    }
    assert(changed == 1);

    /* Now fifteen voices at 50 and 999 at 40: the next start steals 999.
     * Then all are at 50 and the oldest (source 100) goes. */
    assert(mixer_start(t, 1000, 50, 0));
    mixer_render(out, 1);
    for (int i = 0; i < MIXER_VOICES; i++) {
        mixer_voice_info v;
        mixer_get_voice(i, &v);
        assert(v.source_id != 999);
    }
    assert(mixer_start(t, 1001, 50, 0));
    mixer_render(out, 1);
    for (int i = 0; i < MIXER_VOICES; i++) {
        mixer_voice_info v;
        mixer_get_voice(i, &v);
        assert(v.source_id != 100); /* oldest of the 50s */
    }
    mixer_shutdown();
}

/* (c) Restarting the same source cuts its old voice; update and stop work. */
static void test_source_cut(void)
{
    mixer_init(NULL);
    mixer_sound *s = make_sound("a", 1000, gen_one);
    mixer_sound *t = make_sound("b", 1000, gen_one);
    static int16_t out[200 * 2];
    assert(mixer_start(s, 7, 64, 0));
    mixer_render(out, 100);
    assert(active_count() == 1);
    assert(mixer_start(t, 7, 64, 0));
    mixer_render(out, 100);
    assert(active_count() == 1);
    mixer_voice_info v;
    mixer_get_voice(0, &v);
    assert(v.sound == t && v.pos == 100 && v.source_id == 7);
    assert(out[0] == 100 * 256); /* one voice, not two summed */

    /* Anonymous sources never cut each other. */
    assert(mixer_start(s, MIXER_SOURCE_NONE, 64, 0));
    assert(mixer_start(s, MIXER_SOURCE_NONE, 64, 0));
    mixer_render(out, 1);
    assert(active_count() == 3);

    assert(mixer_update(7, 64, MIXER_PAN_RIGHT));
    mixer_render(out, 1);
    mixer_get_voice(0, &v);
    assert(v.pan == MIXER_PAN_RIGHT);
    assert(mixer_stop_source(7));
    mixer_render(out, 1);
    assert(active_count() == 2);
    mixer_shutdown();
}

/* (d) 16 full-scale voices clip to int16 without wraparound. */
static void test_clip(void)
{
    mixer_init(NULL);
    mixer_sound *hi = make_sound("hi", 100, gen_max);
    mixer_sound *lo = make_sound("lo", 100, gen_min);
    static int16_t out[100 * 2];
    for (int i = 0; i < MIXER_VOICES; i++)
        assert(mixer_start(hi, 0, 64, 0));
    mixer_render(out, 100);
    for (int i = 0; i < 200; i++)
        assert(out[i] == INT16_MAX);
    for (int i = 0; i < MIXER_VOICES; i++)
        assert(mixer_start(lo, 0, 64, 0));
    mixer_render(out, 100);
    for (int i = 0; i < 200; i++)
        assert(out[i] == INT16_MIN);
    mixer_shutdown();
}

/* (e) Hard left is silent on the right, and the pan steps are symmetric. */
static void test_pan(void)
{
    mixer_init(NULL);
    mixer_sound *s = make_sound("p", 100, gen_one);
    static int16_t out[100 * 2];
    assert(mixer_start(s, 1, 64, MIXER_PAN_LEFT));
    mixer_render(out, 100);
    for (int i = 0; i < 100; i++)
        assert(out[2 * i] == 100 * 256 && out[2 * i + 1] == 0);
    assert(mixer_start(s, 1, 64, 2));
    mixer_render(out, 10);
    assert(out[0] == 100 * 256 / 2 && out[1] == 100 * 256);
    mixer_shutdown();

    /* Angle -> pan: 0 east, counter-clockwise. Facing east, a source to the
     * north (+90) is on the left. */
    assert(mixer_pan(0, 0) == 0);
    assert(mixer_pan(0, ANG90) == MIXER_PAN_LEFT);
    assert(mixer_pan(0, ANG270) == MIXER_PAN_RIGHT);
    assert(mixer_pan(0, ANG180) == 0);
    assert(mixer_pan(ANG90, ANG180) == MIXER_PAN_LEFT);
    assert(mixer_pan(0, ANG45) == -3 && mixer_pan(0, (angle_t)-ANG45) == 3);
    for (int a = 0; a < 65536; a += 64) {
        int p = mixer_pan(0, (angle_t)a);
        assert(p >= -4 && p <= 4);
        assert(p == -mixer_pan(0, (angle_t)(65536 - a)) || a == 0);
    }
}

/* (f) Distance volume: full at 2 u, 0 at 24 u, monotonic. */
static void test_dist(void)
{
    assert(mixer_dist_volume(0) == 64);
    assert(mixer_dist_volume(2 * FIX_ONE) == 64);
    assert(mixer_dist_volume(24 * FIX_ONE) == 0);
    assert(mixer_dist_volume(1000 * FIX_ONE) == 0);
    assert(mixer_dist_volume(INT32_MAX) == 0);
    assert(mixer_dist_volume(-FIX_ONE) == 64);
    assert(mixer_dist_volume(2 * FIX_ONE + FIX_ONE / 2) < 64);
    assert(mixer_dist_volume(24 * FIX_ONE - FIX_ONE / 2) > 0);
    int prev = 64;
    for (fix_t d = 0; d < 40 * FIX_ONE; d += FIX_ONE / 64) {
        int v = mixer_dist_volume(d);
        assert(v <= prev && v >= 0);
        prev = v;
    }
}

int main(void)
{
    test_unity();
    test_steal();
    test_source_cut();
    test_clip();
    test_pan();
    test_dist();
    printf("mixer: all tests passed\n");
    return 0;
}
