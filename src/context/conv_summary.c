#include "nevercode.h"
#include "defaults.h"

typedef struct {
    char title[CONV_SUMMARY_TITLE_MAX];
    char body[CONV_SUMMARY_BODY_MAX];
    int turns;
    int tools;
    time_t started;
    time_t updated;
    bool active;
} cs_entry_t;

static cs_entry_t g_cs[CONV_SUMMARY_MAX];
static int g_cn = 0;

static void trunc_word(char *b, int m) {
    if ((int)strlen(b) <= m)
        return;
    b[m] = '\0';
    int i = m - 1;
    while (i > 0 && b[i] != ' ')
        i--;
    if (i > 0)
        memcpy(b + i, "...", 4);
}

static void first_sentence(const char *t, char *o, int c) {
    int l = 0;
    for (int i = 0; t[i] && l < c - 4; i++) {
        o[l++] = t[i];
        if ((t[i] == '.' || t[i] == '!' || t[i] == '?') &&
            (t[i + 1] == '\0' || t[i + 1] == ' ' || t[i + 1] == '\n'))
            break;
        if (t[i] == '\n') {
            l--;
            break;
        }
    }
    o[l] = '\0';
    trunc_word(o, c - 4);
}

int csummary_create(const char *msg) {
    if (g_cn >= CONV_SUMMARY_MAX)
        return -1;
    cs_entry_t *e = &g_cs[g_cn];
    memset(e, 0, sizeof(*e));
    e->started = e->updated = nc_time_now();
    e->active = true;
    if (msg) {
        first_sentence(msg, e->title, CONV_SUMMARY_TITLE_MAX);
        nc_strlcpy(e->body, msg, CONV_SUMMARY_BODY_MAX);
        trunc_word(e->body, CONV_SUMMARY_BODY_MAX - 4);
    }
    return g_cn++;
}

int csummary_update(int i, int tc, int tl, const char *t) {
    if (i < 0 || i >= g_cn)
        return -1;
    g_cs[i].turns = tc;
    g_cs[i].tools = tl;
    g_cs[i].updated = nc_time_now();
    if (t && *t) {
        nc_strlcpy(g_cs[i].body, t, CONV_SUMMARY_BODY_MAX);
        trunc_word(g_cs[i].body, CONV_SUMMARY_BODY_MAX - 4);
    }
    return 0;
}

int csummary_set_title(int i, const char *t) {
    if (i < 0 || i >= g_cn || !t)
        return -1;
    nc_strlcpy(g_cs[i].title, t, CONV_SUMMARY_TITLE_MAX);
    return 0;
}

void csummary_close(int i) {
    if (i >= 0 && i < g_cn)
        g_cs[i].active = false;
}
const char *csummary_title(int i) {
    return (i >= 0 && i < g_cn) ? g_cs[i].title : NULL;
}
const char *csummary_body(int i) {
    return (i >= 0 && i < g_cn) ? g_cs[i].body : NULL;
}
int csummary_turns(int i) {
    return (i >= 0 && i < g_cn) ? g_cs[i].turns : 0;
}
int csummary_tools(int i) {
    return (i >= 0 && i < g_cn) ? g_cs[i].tools : 0;
}
bool csummary_is_active(int i) {
    return i >= 0 && i < g_cn && g_cs[i].active;
}
int csummary_count(void) {
    return g_cn;
}

int csummary_active_count(void) {
    int c = 0;
    for (int i = 0; i < g_cn; i++)
        if (g_cs[i].active)
            c++;
    return c;
}

int csummary_format(int i, char *o, int c) {
    if (i < 0 || i >= g_cn)
        return 0;
    cs_entry_t *e = &g_cs[i];
    char ts[NC_FMT_MAX];
    struct tm *tm = localtime(&e->updated);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", tm);
    return snprintf(o, c, "[%s] %s (%d turns, %d tools)%s", ts, e->title, e->turns, e->tools,
                    e->active ? "" : "[closed]");
}

void csummary_clear(void) {
    g_cn = 0;
    memset(g_cs, 0, sizeof(g_cs));
}

int csummary_find_by_title(const char *s) {
    if (!s)
        return -1;
    for (int i = g_cn - 1; i >= 0; i--)
        if (strstr(g_cs[i].title, s))
            return i;
    return -1;
}
