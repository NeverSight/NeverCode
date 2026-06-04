/*
 * msg_collapse.c — Collapse consecutive tool-use messages for cleaner UI
 *
 * Groups runs of read/search/glob operations, background-task completions,
 * and hook summaries into single collapsed entries.  Verbose mode bypasses
 * all collapsing so the full trace is available on demand.
 */

#include "nevercode.h"
#include "terminal.h"

/* ── Tool-class detection ── */

typedef enum {
    TC_NONE,
    TC_READ,
    TC_SEARCH,
    TC_LIST,
} tool_class_t;

static tool_class_t classify_tool_name(const char *name) {
    if (!name)
        return TC_NONE;
    if (strcmp(name, "file_read") == 0 || strcmp(name, "FileRead") == 0)
        return TC_READ;
    if (strcmp(name, "grep") == 0 || strcmp(name, "Grep") == 0 || strcmp(name, "glob") == 0 ||
        strcmp(name, "Glob") == 0 || strcmp(name, "file_search") == 0 ||
        strcmp(name, "ToolSearch") == 0 || strcmp(name, "web_search") == 0)
        return TC_SEARCH;
    if (strcmp(name, "listdir") == 0 || strcmp(name, "ListDir") == 0)
        return TC_LIST;
    return TC_NONE;
}

static bool is_collapsible(tool_class_t tc) {
    return tc == TC_READ || tc == TC_SEARCH || tc == TC_LIST;
}

/* Extract the first tool_name from a message's content blocks. */
static const char *msg_first_tool_name(const message_t *m) {
    for (int i = 0; i < m->num_blocks; i++) {
        if (m->blocks[i].type == CONTENT_TOOL_USE && m->blocks[i].tool_name)
            return m->blocks[i].tool_name;
    }
    return NULL;
}

/* Extract a path hint from tool input JSON. */
static const char *msg_tool_path_hint(const message_t *m) {
    for (int i = 0; i < m->num_blocks; i++) {
        if (m->blocks[i].type == CONTENT_TOOL_USE && m->blocks[i].tool_input) {
            cJSON *fp = cJSON_GetObjectItem(m->blocks[i].tool_input, "file_path");
            if (fp && cJSON_IsString(fp))
                return fp->valuestring;
            cJSON *p = cJSON_GetObjectItem(m->blocks[i].tool_input, "path");
            if (p && cJSON_IsString(p))
                return p->valuestring;
        }
    }
    return NULL;
}

/* ── Collapse group ── */

typedef struct {
    int reads;
    int searches;
    int lists;
    int first_idx;
    int last_idx;
    char paths[MSG_COLLAPSE_MAX_PATHS][MSG_COLLAPSE_PATH_MAX];
    int path_count;
} collapse_group_t;

static void group_init(collapse_group_t *g, int idx) {
    memset(g, 0, sizeof(*g));
    g->first_idx = idx;
    g->last_idx = idx;
}

static void group_add_path(collapse_group_t *g, const char *path) {
    if (!path || !*path || g->path_count >= MSG_COLLAPSE_MAX_PATHS)
        return;
    for (int i = 0; i < g->path_count; i++) {
        if (strcmp(g->paths[i], path) == 0)
            return;
    }
    nc_strlcpy(g->paths[g->path_count], path, MSG_COLLAPSE_PATH_MAX);
    g->path_count++;
}

/* ── Public API ── */

int msg_collapse_scan(const message_t *msgs, int len, msg_collapse_entry_t *out, int out_cap) {
    if (!msgs || len <= 0 || !out || out_cap <= 0)
        return 0;

    int n = 0;
    int i = 0;

    while (i < len && n < out_cap) {
        const char *tname = msg_first_tool_name(&msgs[i]);
        tool_class_t tc = classify_tool_name(tname);

        if (!is_collapsible(tc)) {
            out[n].kind = MSG_COLLAPSE_PASSTHROUGH;
            out[n].start = i;
            out[n].count = 1;
            out[n].reads = 0;
            out[n].searches = 0;
            out[n].lists = 0;
            out[n].path_hint[0] = '\0';
            n++;
            i++;
            continue;
        }

        collapse_group_t g;
        group_init(&g, i);

        while (i < len) {
            const char *tn = msg_first_tool_name(&msgs[i]);
            tool_class_t cur = classify_tool_name(tn);
            if (!is_collapsible(cur))
                break;

            if (cur == TC_READ)
                g.reads++;
            else if (cur == TC_SEARCH)
                g.searches++;
            else if (cur == TC_LIST)
                g.lists++;

            const char *ph = msg_tool_path_hint(&msgs[i]);
            group_add_path(&g, ph);

            g.last_idx = i;
            i++;
        }

        int total = g.reads + g.searches + g.lists;
        if (total <= 1) {
            out[n].kind = MSG_COLLAPSE_PASSTHROUGH;
            out[n].start = g.first_idx;
            out[n].count = 1;
            out[n].reads = g.reads;
            out[n].searches = g.searches;
            out[n].lists = g.lists;
            out[n].path_hint[0] = '\0';
        } else {
            out[n].kind = MSG_COLLAPSE_READ_SEARCH;
            out[n].start = g.first_idx;
            out[n].count = g.last_idx - g.first_idx + 1;
            out[n].reads = g.reads;
            out[n].searches = g.searches;
            out[n].lists = g.lists;

            if (g.path_count == 1)
                nc_strlcpy(out[n].path_hint, g.paths[0], sizeof(out[n].path_hint));
            else if (g.path_count > 1)
                snprintf(out[n].path_hint, sizeof(out[n].path_hint), "%d files", g.path_count);
            else
                out[n].path_hint[0] = '\0';
        }
        n++;
    }
    return n;
}

char *msg_collapse_format(const msg_collapse_entry_t *entry) {
    if (!entry || entry->kind != MSG_COLLAPSE_READ_SEARCH)
        return NULL;

    char buf[NC_MSG_MAX];
    int off = 0;

    off += snprintf(buf + off, sizeof(buf) - off, "[");

    bool needs_sep = false;
    if (entry->reads > 0) {
        off += snprintf(buf + off, sizeof(buf) - off, "%d read%s", entry->reads,
                        entry->reads != 1 ? "s" : "");
        needs_sep = true;
    }
    if (entry->searches > 0) {
        if (needs_sep)
            off += snprintf(buf + off, sizeof(buf) - off, " + ");
        off += snprintf(buf + off, sizeof(buf) - off, "%d search%s", entry->searches,
                        entry->searches != 1 ? "es" : "");
        needs_sep = true;
    }
    if (entry->lists > 0) {
        if (needs_sep)
            off += snprintf(buf + off, sizeof(buf) - off, " + ");
        off += snprintf(buf + off, sizeof(buf) - off, "%d list%s", entry->lists,
                        entry->lists != 1 ? "s" : "");
    }

    if (entry->path_hint[0])
        off += snprintf(buf + off, sizeof(buf) - off, " | %s", entry->path_hint);

    snprintf(buf + off, sizeof(buf) - off, "]");

    return nc_xstrdup(buf);
}

int msg_collapse_hook_summaries(const message_t *msgs, int len, int *merged_indices, int cap) {
    if (!msgs || len <= 0 || !merged_indices || cap <= 0)
        return 0;

    int n = 0;
    int i = 0;

    while (i < len && n < cap) {
        if (msgs[i].role != ROLE_SYSTEM) {
            i++;
            continue;
        }

        const char *label = msg_first_tool_name(&msgs[i]);
        if (!label) {
            i++;
            continue;
        }
        int run_start = i;
        int run_len = 0;
        while (i < len && msgs[i].role == ROLE_SYSTEM) {
            const char *cur = msg_first_tool_name(&msgs[i]);
            if (!cur || strcmp(cur, label) != 0)
                break;
            run_len++;
            i++;
        }
        if (run_len > 1 && n < cap) {
            merged_indices[n++] = run_start;
        }
    }
    return n;
}
