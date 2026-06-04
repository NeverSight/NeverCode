/*
 * color_palette.c — adaptive terminal color palette
 *
 * Detects terminal color capability (truecolor / 256 / 16 / none)
 * and maps semantic roles to the best available representation.
 */

#include "nevercode.h"
#include "defaults.h"
#include "terminal.h"

/* ── color capability detection ── */

typedef enum {
    CAP_NONE = 0,
    CAP_BASIC, /* 8/16 colors */
    CAP_256,   /* xterm-256color */
    CAP_TRUE   /* 24-bit RGB */
} color_cap_t;

static color_cap_t g_cap = CAP_NONE;
static bool g_init = false;

static color_cap_t detect_capability(void) {
    if (td_has_truecolor())
        return CAP_TRUE;

    const char *term = nc_term_env();
    if (!term)
        return isatty(STDOUT_FILENO) ? CAP_BASIC : CAP_NONE;
    if (strstr(term, "256color"))
        return CAP_256;
    if (strstr(term, "color") || strstr(term, "xterm") || strstr(term, "screen"))
        return CAP_BASIC;
    return isatty(STDOUT_FILENO) ? CAP_BASIC : CAP_NONE;
}

void palette_init(void) {
    g_cap = detect_capability();
    g_init = true;
}

void palette_free(void) {
    g_cap = CAP_NONE;
    g_init = false;
}

int palette_capability(void) {
    if (!g_init)
        palette_init();
    return (int)g_cap;
}

/* ── role → ANSI string mapping ── */

typedef struct {
    const char *rgb;   /* "\033[38;2;R;G;Bm" */
    const char *c256;  /* "\033[38;5;Nm" */
    const char *basic; /* "\033[XXm" */
} role_map_t;

static const role_map_t ROLES[PAL_ROLE_COUNT] = {
    [PAL_ROLE_HEADER] = {"\033[38;2;0;175;215m", "\033[38;5;38m", "\033[1;36m"},
    [PAL_ROLE_TOOL] = {"\033[38;2;215;175;0m", "\033[38;5;178m", "\033[1;33m"},
    [PAL_ROLE_ERROR] = {"\033[38;2;215;0;0m", "\033[38;5;160m", "\033[1;31m"},
    [PAL_ROLE_SUCCESS] = {"\033[38;2;0;175;0m", "\033[38;5;34m", "\033[1;32m"},
    [PAL_ROLE_MUTED] = {"\033[38;2;128;128;128m", "\033[38;5;244m", "\033[90m"},
    [PAL_ROLE_ACCENT] = {"\033[38;2;95;135;255m", "\033[38;5;69m", "\033[1;34m"},
    [PAL_ROLE_WARN] = {"\033[38;2;255;175;0m", "\033[38;5;214m", "\033[33m"},
    [PAL_ROLE_RESET] = {T_RESET, T_RESET, T_RESET},
};

const char *palette_seq(int role) {
    if (!g_init)
        palette_init();
    if (role < 0 || role >= PAL_ROLE_COUNT)
        role = PAL_ROLE_RESET;

    switch (g_cap) {
    case CAP_TRUE:
        return ROLES[role].rgb;
    case CAP_256:
        return ROLES[role].c256;
    case CAP_BASIC:
        return ROLES[role].basic;
    default:
        return "";
    }
}

const char *palette_reset(void) {
    return palette_seq(PAL_ROLE_RESET);
}

void palette_sprint(char *dst, size_t sz, int role, const char *text) {
    if (!dst || !sz)
        return;
    if (!text)
        text = "";
    if (g_cap == CAP_NONE) {
        nc_strlcpy(dst, text, sz);
        return;
    }
    snprintf(dst, sz, "%s%s%s", palette_seq(role), text, palette_reset());
}

char *palette_wrap(int role, const char *text) {
    if (!text)
        text = "";
    size_t need = strlen(palette_seq(role)) + strlen(text) + strlen(palette_reset()) + 1;
    char *buf = malloc(need);
    if (!buf)
        return NULL;
    snprintf(buf, need, "%s%s%s", palette_seq(role), text, palette_reset());
    return buf;
}

void palette_force_cap(int cap) {
    g_cap = (color_cap_t)cap;
    g_init = true;
}
