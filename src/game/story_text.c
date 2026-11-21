#include "game/story_text.h"

#include <string.h>

/* Copied from docs/design/story.md (Intro, Intermissions, Ending, Credits).
 * Keep in sync by hand when story.md changes. */

const char *const story_intro[STORY_INTRO_SCREENS] = {
    "THE CITY OF CATLANTIS, DAWN.\n"
    "\n"
    "SERGEANT MAU OF THE ROYAL WHISKER\n"
    "GUARD WAKES UP IN HER FAVOURITE\n"
    "SUNBEAM. THE SUNBEAM IS NOW BEHIND\n"
    "A STEEL FENCE. THE FENCE HAS A SIGN.\n"
    "\n"
    "THE SIGN SAYS \"GOOD KITTY. STAY.\"",

    "OVERNIGHT, THE KENNEL DOMINION HAS\n"
    "TAKEN THE CITY. THEIR CYBORG\n"
    "OVERLORD, PHARAOH WOOFU, WANTS THE\n"
    "GREAT SPHINX TO BECOME THE GREAT\n"
    "SPHOUND.\n"
    "\n"
    "MAU STRETCHES. MAU SHARPENS HER\n"
    "CLAWS ON THE SIGN. MAU HAS NOT HAD\n"
    "BREAKFAST.\n"
    "\n"
    "SOMEBODY IS GOING TO PAY.",
};

const char *const story_intermission[STORY_INTERMISSIONS] = {
    "THE GATE IS OPEN. INSIDE, THE MEW-SEUM IS BEING LOOTED. NOBODY LOOTS THE MUMMIFIED "
    "MICE. THOSE ARE FOR LATER.",
    "THE MUSEUM IS SAFE. THE GIFT SHOP IS NOT. MAU TAKES A SOUVENIR SCATTERGUN AND HEADS "
    "FOR THE DOCKS, WHERE THE DOGS ARE PUMPING THE NILE INTO SOMETHING CALLED \"GRAVY\".",
    "THE PUMPS ARE DOWN. A CAPTURED POMERANIAN, BRIBED WITH A BRUSHING, REVEALS THAT "
    "WOOFU'S ENGINEERS ARE DIGGING UP THE TOMB OF TUTANKHAMEW. NOBODY DISTURBS THE "
    "PHARAOH'S NAP.",
    "THE TOMB IS SEALED AGAIN. TUTANKHAMEW ROLLED OVER AND KEPT SLEEPING. MAU FOLLOWS THE "
    "DIGGING CREWS' TRACKS TO A FACTORY THAT SMELLS OF KIBBLE AND REGRET.",
    "THE REFINERY IS SCRAP. ITS LAST SHIPMENT WAS ADDRESSED TO \"THE GREAT SPHOUND, C/O "
    "WOOFU\". MAU WILL BE DELIVERING IT PERSONALLY.",
    "THE SPHINX STILL HAS ITS FACE, MOSTLY. THE GRINDER IS SILENT. ON THE HORIZON, A "
    "CHROME PYRAMID HUMS WITH THE SIGNAL THAT WILL MAKE EVERY CAT IN CATLANTIS SIT.",
    "THE SERVERS ARE DOWN AND THE UPLINK IS UNPLUGGED. ONE FLOOR UP, WOOFU IS POLISHING "
    "HIS SARCOPHAGUS. HE SHOULD HAVE POLISHED HIS EXIT STRATEGY.",
};

const char *const story_ending[STORY_ENDING_SCREENS] = {
    "WOOFU'S SARCOPHAGUS CLATTERS DOWN\n"
    "THE PYRAMID STEPS, ONE STEP AT A\n"
    "TIME, ALL NINE HUNDRED OF THEM.\n"
    "\n"
    "THE BROADCAST GOES SILENT. ACROSS\n"
    "CATLANTIS, NOT ONE CAT SITS.\n"
    "CATS HAVE NEVER SAT ON COMMAND.\n"
    "THAT WAS THE FLAW IN THE PLAN.",

    "THE PHARAOH WAKES, YAWNS, AND\n"
    "DECLARES A NATIONAL HOLIDAY IN\n"
    "SERGEANT MAU'S HONOUR.\n"
    "\n"
    "MAU SLEEPS THROUGH IT.\n"
    "\n"
    "FAR TO THE NORTH, IN THE KENNEL\n"
    "DOMINION'S HOMELAND, SOMETHING\n"
    "LARGE STARTS BARKING.\n"
    "\n"
    "      END OF EPISODE ONE",
};

/* Credits: one string per line, drawn centred, scrolled (not wrapped).
 * The widest line is 36 characters (360 px in the 10 px menu font), so
 * the credits use the small font (flow.c). */
const char *const story_credits[] = {
    "VIBE SHOOTER 3D II",
    "EPISODE ONE: PAWS OF FURY",
    "",
    "DESIGN ............ THE DESIGNER AGENT",
    "ENGINE ............ THE DEVELOPER AGENTS",
    "ART DIRECTION ..... THE ART DIRECTOR AGENT",
    "REVIEW ............ THE REVIEWER AGENT",
    "PLAYTESTING ....... THE PLAYTESTER AGENT",
    "PRODUCED BY ....... MALESPIAUT",
    "",
    "BUILT FROM SCRATCH IN C17 WITH SDL3,",
    "PYTHON, BLENDER AND ASEPRITE.",
    "",
    "NO DOGS WERE HARMED. SEVERAL ROBOTS",
    "WERE MILDLY INCONVENIENCED.",
    "",
    "THANK YOU FOR PLAYING.",
};
const int story_credits_lines = (int)(sizeof story_credits / sizeof story_credits[0]);

int story_wrap(const char *src, int cols, char *out, size_t cap)
{
    size_t o = 0;
    int lines = 0, col = 0;
    const char *p = src;
    int para_start = 1;
    if (cap == 0)
        return 0;
    while (*p) {
        if (p[0] == '\n' && p[1] == '\n') { /* paragraph break */
            while (*p == '\n')
                p++;
            if (col > 0 && o + 3 < cap) {
                out[o++] = '\n';
                out[o++] = '\n';
                lines += 2;
                col = 0;
                para_start = 1;
            }
            continue;
        }
        if (*p == ' ' && para_start && col == 0) { /* keep a paragraph's indent */
            int n = 0;
            while (p[n] == ' ')
                n++;
            if (n < cols && o + (size_t)n + 1 < cap) {
                memset(out + o, ' ', (size_t)n);
                o += (size_t)n;
                col = n;
            }
            p += n;
            continue;
        }
        if (*p == ' ' || *p == '\n') {
            p++;
            continue;
        }
        const char *w = p;
        while (*p && *p != ' ' && *p != '\n')
            p++;
        int len = (int)(p - w);
        int indent_only = para_start && col > 0;
        if (col > 0 && !indent_only && col + 1 + len > cols) {
            if (o + 1 >= cap)
                break;
            out[o++] = '\n';
            lines++;
            col = 0;
        } else if (col > 0 && !indent_only) {
            if (o + 1 >= cap)
                break;
            out[o++] = ' ';
            col++;
        }
        if (o + (size_t)len + 1 >= cap)
            break;
        memcpy(out + o, w, (size_t)len);
        o += (size_t)len;
        col += len;
        para_start = 0;
    }
    out[o] = '\0';
    return col > 0 ? lines + 1 : lines;
}

/* Start of line `n` in wrapped text, or NULL. */
static const char *line_at(const char *s, int n)
{
    while (n > 0 && s) {
        s = strchr(s, '\n');
        if (s)
            s++;
        n--;
    }
    return s;
}

/* Wrapped text and its lines with the blank lines that would open a
 * continuation page skipped: the page starts are computed greedily. */
static int paginate(const char *src, char *buf, size_t cap, int *starts, int max_pages)
{
    int lines = story_wrap(src, STORY_COLS, buf, cap);
    int pages = 0, at = 0;
    while (at < lines && pages < max_pages) {
        const char *l = line_at(buf, at);
        if (pages > 0 && l && *l == '\n') { /* blank line at a page top */
            at++;
            continue;
        }
        starts[pages++] = at;
        at += STORY_LINES;
    }
    starts[pages] = lines;
    return pages;
}

#define STORY_BUF 4096
#define STORY_MAX_PAGES 8

int story_page_count(const char *src)
{
    static char buf[STORY_BUF];
    int starts[STORY_MAX_PAGES + 1];
    return paginate(src, buf, sizeof buf, starts, STORY_MAX_PAGES);
}

int story_page(const char *src, int page, char *out, size_t cap)
{
    static char buf[STORY_BUF];
    int starts[STORY_MAX_PAGES + 1];
    int pages = paginate(src, buf, sizeof buf, starts, STORY_MAX_PAGES);
    if (page < 0 || page >= pages || cap == 0)
        return 0;
    int end = starts[page] + STORY_LINES;
    if (page + 1 < pages && end > starts[page + 1])
        end = starts[page + 1];
    const char *a = line_at(buf, starts[page]);
    const char *b = line_at(buf, end);
    size_t n = b ? (size_t)(b - a - 1) : strlen(a);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, a, n);
    out[n] = '\0';
    return 1;
}
