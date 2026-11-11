/* ctest `jukebox`: the level -> track table (audio.md, Music) and the game's
 * music player: plays by name, logs `<tic> music <name>` on every change,
 * reports a missing track once and plays silence.
 *   test_jukebox <music dir> <tmp file> */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/music.h"
#include "game/jukebox.h"

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    static char buf[4096];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    const char *dir = argv[1], *logpath = argv[2];

    assert(strcmp(music_track_for_level(1), "level_eg") == 0);
    assert(strcmp(music_track_for_level(2), "level_eg") == 0);
    assert(strcmp(music_track_for_level(3), "level_docks") == 0);
    assert(strcmp(music_track_for_level(4), "level_tomb") == 0);
    assert(strcmp(music_track_for_level(5), "level_dt") == 0);
    assert(strcmp(music_track_for_level(6), "level_dt") == 0);
    assert(strcmp(music_track_for_level(7), "level_climax") == 0);
    assert(strcmp(music_track_for_level(8), "boss") == 0);
    assert(music_track_for_level(0) == NULL);
    assert(music_track_for_level(9) == NULL);

    FILE *log = fopen(logpath, "w");
    assert(log);
    jukebox_init(dir, 0, log);
    assert(jukebox_play("title", 0) == 1);       /* exists in assets/src/music */
    assert(strcmp(jukebox_current(), "title") == 0);
    assert(jukebox_play("no_such_track", 5) == 0); /* missing: silence */
    assert(strcmp(jukebox_current(), "no_such_track") == 0);
    assert(jukebox_missing_reports() == 1);
    assert(jukebox_play("no_such_track", 9) == 0); /* restart: no second report */
    assert(jukebox_missing_reports() == 1);
    assert(jukebox_play("title", 12) == 1);        /* cached */
    jukebox_stop(20);
    assert(jukebox_current() == NULL);
    assert(jukebox_play("../title", 21) == 0);     /* names are bare */
    jukebox_shutdown();
    fclose(log);

    const char *want = "0 music title\n"
                       "5 music no_such_track\n"
                       "9 music no_such_track\n"
                       "12 music title\n"
                       "20 music -\n";
    const char *got = slurp(logpath);
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "log mismatch:\n%s---\nwant:\n%s", got, want);
        return 1;
    }
    puts("jukebox: ok");
    return 0;
}
