#include "nevercode.h"
#include "defaults.h"
#include "tools.h"

typedef struct {
    char name[DYN_TOOL_NAME_MAX];
    char description[DYN_TOOL_DESC_MAX];
    bool required;
} dt_param_t;

typedef int (*dyn_tool_fn)(const char *input, char *output, int out_cap);

typedef struct {
    char name[DYN_TOOL_NAME_MAX];
    char description[DYN_TOOL_DESC_MAX];
    char source[DYN_TOOL_NAME_MAX];
    dt_param_t params[DYN_TOOL_PARAM_MAX];
    int param_count;
    dyn_tool_fn handler;
    bool active;
    int priority;
} dt_entry_t;

static dt_entry_t g_dt[DYN_TOOL_MAX_ENTRIES];
static int g_dc = 0;

static dt_entry_t *dt_find(const char *n) {
    for (int i = 0; i < g_dc; i++)
        if (g_dt[i].active && strcmp(g_dt[i].name, n) == 0)
            return &g_dt[i];
    return NULL;
}

int dyntool_register(const char *n, const char *d, const char *s, dyn_tool_fn h) {
    if (!n || !h)
        return -1;
    if (dt_find(n))
        return -2;
    if (g_dc >= DYN_TOOL_MAX_ENTRIES)
        return -3;
    dt_entry_t *t = &g_dt[g_dc++];
    memset(t, 0, sizeof(*t));
    nc_strlcpy(t->name, n, DYN_TOOL_NAME_MAX);
    if (d)
        nc_strlcpy(t->description, d, DYN_TOOL_DESC_MAX);
    if (s)
        nc_strlcpy(t->source, s, DYN_TOOL_NAME_MAX);
    t->handler = h;
    t->active = true;
    return 0;
}

int dyntool_add_param(const char *tn, const char *pn, const char *pd, bool req) {
    dt_entry_t *t = dt_find(tn);
    if (!t || t->param_count >= DYN_TOOL_PARAM_MAX)
        return -1;
    dt_param_t *p = &t->params[t->param_count++];
    nc_strlcpy(p->name, pn, DYN_TOOL_NAME_MAX);
    if (pd)
        nc_strlcpy(p->description, pd, DYN_TOOL_DESC_MAX);
    p->required = req;
    return 0;
}

int dyntool_unregister(const char *n) {
    dt_entry_t *t = dt_find(n);
    if (!t)
        return -1;
    t->active = false;
    return 0;
}
int dyntool_invoke(const char *n, const char *in, char *out, int oc) {
    dt_entry_t *t = dt_find(n);
    return (t && t->handler) ? t->handler(in, out, oc) : -1;
}
bool dyntool_exists(const char *n) {
    return n && dt_find(n) != NULL;
}

int dyntool_count(void) {
    int c = 0;
    for (int i = 0; i < g_dc; i++)
        if (g_dt[i].active)
            c++;
    return c;
}

int dyntool_list(const char **out, int cap) {
    int c = 0;
    for (int i = 0; i < g_dc && c < cap; i++)
        if (g_dt[i].active)
            out[c++] = g_dt[i].name;
    return c;
}

const char *dyntool_description(const char *n) {
    dt_entry_t *t = dt_find(n);
    return t ? t->description : NULL;
}
const char *dyntool_source(const char *n) {
    dt_entry_t *t = dt_find(n);
    return t ? t->source : NULL;
}
int dyntool_param_count(const char *n) {
    dt_entry_t *t = dt_find(n);
    return t ? t->param_count : 0;
}
void dyntool_set_priority(const char *n, int p) {
    dt_entry_t *t = dt_find(n);
    if (t)
        t->priority = p;
}
void dyntool_clear_all(void) {
    g_dc = 0;
    memset(g_dt, 0, sizeof(g_dt));
}

int dyntool_list_by_source(const char *s, const char **out, int cap) {
    int c = 0;
    for (int i = 0; i < g_dc && c < cap; i++)
        if (g_dt[i].active && strcmp(g_dt[i].source, s) == 0)
            out[c++] = g_dt[i].name;
    return c;
}
