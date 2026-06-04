/*
 * term_detect.c — Terminal capability detection.
 *
 * Probes the current terminal for color support, dimensions, Unicode
 * width, multiplexer presence (tmux/screen), and true-color capability.
 * Results are cached after first probe.
 */
#include "nevercode.h"
#include "defaults.h"
#include <sys/ioctl.h>

typedef struct {
    int cols;
    int rows;
    bool color;
    bool truecolor;
    bool unicode;
    bool is_tmux;
    bool is_screen;
    bool is_ssh;
    bool is_dumb;
    bool probed;
    char term_type[NC_FMT_MAX];
    char colorterm[NC_FMT_MAX];
} term_caps_t;

static term_caps_t g_caps;

static void detect_once(void) {
    if (g_caps.probed)
        return;
    g_caps.probed = true;
    const char *term = nc_term_env();
    const char *ct = getenv(ENV_COLORTERM);
    const char *tmux = getenv(ENV_TMUX);
    const char *sty = getenv(ENV_STY);
    const char *ssh = getenv(ENV_SSH_CONN);
    if (term)
        nc_strlcpy(g_caps.term_type, term, sizeof(g_caps.term_type));
    if (ct)
        nc_strlcpy(g_caps.colorterm, ct, sizeof(g_caps.colorterm));
    g_caps.is_dumb = term && strcmp(term, "dumb") == 0;
    g_caps.is_tmux = tmux && tmux[0];
    g_caps.is_screen = sty && sty[0];
    g_caps.is_ssh = ssh && ssh[0];
    g_caps.color = !g_caps.is_dumb && isatty(STDOUT_FILENO);
    g_caps.truecolor = ct && (strcmp(ct, "truecolor") == 0 || strcmp(ct, "24bit") == 0);
    g_caps.unicode = true;
    const char *lang = getenv(ENV_LANG);
    if (lang && !strstr(lang, "UTF") && !strstr(lang, "utf"))
        g_caps.unicode = false;
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        g_caps.cols = ws.ws_col > 0 ? ws.ws_col : TD_DEFAULT_COLS;
        g_caps.rows = ws.ws_row > 0 ? ws.ws_row : TD_DEFAULT_ROWS;
    } else {
        g_caps.cols = TD_DEFAULT_COLS;
        g_caps.rows = TD_DEFAULT_ROWS;
    }
}

int td_cols(void) {
    detect_once();
    return g_caps.cols;
}
int td_rows(void) {
    detect_once();
    return g_caps.rows;
}
bool td_has_color(void) {
    detect_once();
    return g_caps.color;
}
bool td_has_truecolor(void) {
    detect_once();
    return g_caps.truecolor;
}
bool td_has_unicode(void) {
    detect_once();
    return g_caps.unicode;
}
bool td_is_tmux(void) {
    detect_once();
    return g_caps.is_tmux;
}
bool td_is_screen(void) {
    detect_once();
    return g_caps.is_screen;
}
bool td_is_ssh(void) {
    detect_once();
    return g_caps.is_ssh;
}
bool td_is_dumb(void) {
    detect_once();
    return g_caps.is_dumb;
}
const char *td_term_type(void) {
    detect_once();
    return g_caps.term_type;
}

void td_refresh(void) {
    g_caps.probed = false;
    detect_once();
}

int td_format(char *out, int cap) {
    detect_once();
    return snprintf(out, cap,
                    "term=%s cols=%d rows=%d color=%d truecolor=%d unicode=%d tmux=%d ssh=%d",
                    g_caps.term_type, g_caps.cols, g_caps.rows, g_caps.color, g_caps.truecolor,
                    g_caps.unicode, g_caps.is_tmux, g_caps.is_ssh);
}
