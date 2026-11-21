#ifndef GAME_STORY_TEXT_H
#define GAME_STORY_TEXT_H

/* Text screens (docs/design/story.md), copied from story.md by hand: intro,
 * the intermissions after levels 1-7, ending and credits. Story text uses
 * story.md's line breaks as soft breaks (the #102 decision): story_wrap()
 * joins a paragraph's lines and wraps at spaces; a blank line is a
 * paragraph break. Credits keep their lines exactly. */

#include <stddef.h>

#define STORY_INTRO_SCREENS 2
#define STORY_ENDING_SCREENS 2
#define STORY_INTERMISSIONS 7 /* after levels 1..7 */

/* Text box on the papyrus panel: x 10-309, y 36-203, 30 x 12 menu cells. */
#define STORY_BOX_X 10
#define STORY_BOX_Y 36
#define STORY_COLS 30
#define STORY_LINES 12

extern const char *const story_intro[STORY_INTRO_SCREENS];
extern const char *const story_intermission[STORY_INTERMISSIONS];
extern const char *const story_ending[STORY_ENDING_SCREENS];
extern const char *const story_credits[];
extern const int story_credits_lines;

/* Greedy word wrap of `src` to `cols` characters into `out` ('\n'
 * separated lines). Single newlines are soft; a blank line becomes one
 * empty line between paragraphs. Leading spaces of a paragraph's first line
 * are kept (the centred END OF EPISODE ONE). Returns the line count. */
int story_wrap(const char *src, int cols, char *out, size_t cap);

/* Pages of `src` wrapped to STORY_COLS x STORY_LINES: a screen whose
 * wrapped text needs more than STORY_LINES lines continues on further
 * pages (the #102 decision). */
int story_page_count(const char *src);
/* Page `page` (0-based) of `src` into `out`, blank lines at the top of a
 * continuation page dropped. Returns 0 if `page` is out of range. */
int story_page(const char *src, int page, char *out, size_t cap);

#endif
