#include "game/jukebox.h"

#include <stdlib.h>
#include <string.h>

#include "engine/music.h"

#define JUKEBOX_MAX 16
#define JUKEBOX_NAME 32

static const char *const level_tracks[] = {
    NULL,          "level_eg", "level_eg",     "level_docks", "level_tomb",
    "level_dt",    "level_dt", "level_climax", "boss",
};

const char *music_track_for_level(int level)
{
    if (level < 1 || level >= (int)(sizeof level_tracks / sizeof level_tracks[0]))
        return NULL;
    return level_tracks[level];
}

typedef struct {
    char name[JUKEBOX_NAME];
    music_song *song; /* NULL: missing or broken (reported once) */
} track;

static struct {
    char dir[512];
    int audio;
    FILE *log;
    track tracks[JUKEBOX_MAX];
    int ntracks;
    char current[JUKEBOX_NAME];
    int playing; /* current[] is valid */
    int missing_reports;
} g_jb;

void jukebox_init(const char *dir, int audio, FILE *log)
{
    jukebox_shutdown();
    snprintf(g_jb.dir, sizeof g_jb.dir, "%s", dir);
    g_jb.audio = audio;
    g_jb.log = log;
}

/* Bare names only: [a-z0-9_], shorter than JUKEBOX_NAME. */
static int valid_name(const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n >= JUKEBOX_NAME)
        return 0;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return 0;
    }
    return 1;
}

static const track *find_or_load(const char *name)
{
    for (int i = 0; i < g_jb.ntracks; i++)
        if (strcmp(g_jb.tracks[i].name, name) == 0)
            return &g_jb.tracks[i];
    if (g_jb.ntracks == JUKEBOX_MAX)
        return NULL;
    track *t = &g_jb.tracks[g_jb.ntracks++];
    snprintf(t->name, sizeof t->name, "%s", name);
    char path[600], err[256] = "";
    snprintf(path, sizeof path, "%s/%s.vmus", g_jb.dir, name);
    t->song = music_load(path, err, sizeof err);
    if (!t->song) {
        fprintf(stderr, "music: %s missing (%s)\n", name, err);
        g_jb.missing_reports++;
    }
    return t;
}

static void log_line(uint32_t tic, const char *name)
{
    if (g_jb.log)
        fprintf(g_jb.log, "%lu music %s\n", (unsigned long)tic, name);
}

int jukebox_play(const char *name, uint32_t tic)
{
    if (!valid_name(name)) {
        fprintf(stderr, "music: bad track name '%s'\n", name);
        return 0;
    }
    const track *t = find_or_load(name);
    snprintf(g_jb.current, sizeof g_jb.current, "%s", name);
    g_jb.playing = 1;
    log_line(tic, name);
    if (g_jb.audio) {
        if (t && t->song)
            music_play(t->song);
        else
            music_stop();
    }
    return t && t->song;
}

void jukebox_stop(uint32_t tic)
{
    if (g_jb.audio)
        music_stop();
    if (g_jb.playing)
        log_line(tic, "-");
    g_jb.playing = 0;
}

const char *jukebox_current(void)
{
    return g_jb.playing ? g_jb.current : NULL;
}

int jukebox_missing_reports(void)
{
    return g_jb.missing_reports;
}

void jukebox_shutdown(void)
{
    for (int i = 0; i < g_jb.ntracks; i++)
        music_free(g_jb.tracks[i].song);
    memset(&g_jb, 0, sizeof g_jb);
}
