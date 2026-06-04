/*
 * agent_summary.c — Periodic progress summaries for running sub-agents
 */
#include "nevercode.h"
#include "terminal.h"

#define SUMMARY_MAX_LEN AGENT_SUMMARY_MAX_LEN
#define MAX_AGENT_SUMMARIES MAX_LIVE_AGENTS

typedef struct {
    char agent_id[NC_FMT_MAX];
    char summary[SUMMARY_MAX_LEN];
    long updated_at;
    int turn_count;
} agent_progress_t;

static agent_progress_t g_progress[MAX_AGENT_SUMMARIES];
static int g_progress_count = 0;

static int find_progress(const char *id) {
    for (int i = 0; i < g_progress_count; i++)
        if (strcmp(g_progress[i].agent_id, id) == 0)
            return i;
    return -1;
}

void agent_summary_init(void) {
    g_progress_count = 0;
    memset(g_progress, 0, sizeof(g_progress));
}
void agent_summary_free(void) {
    g_progress_count = 0;
}

int agent_summary_update(const char *agent_id, const char *summary) {
    if (!agent_id || !summary)
        return -1;
    int idx = find_progress(agent_id);
    if (idx < 0) {
        if (g_progress_count >= MAX_AGENT_SUMMARIES)
            return -1;
        idx = g_progress_count++;
        nc_strlcpy(g_progress[idx].agent_id, agent_id, sizeof(g_progress[idx].agent_id));
        g_progress[idx].turn_count = 0;
    }
    nc_strlcpy(g_progress[idx].summary, summary, SUMMARY_MAX_LEN);
    g_progress[idx].updated_at = nc_wall_sec();
    g_progress[idx].turn_count++;
    return 0;
}

int agent_summary_remove(const char *agent_id) {
    int idx = find_progress(agent_id);
    if (idx < 0)
        return -1;
    if (idx < g_progress_count - 1)
        g_progress[idx] = g_progress[g_progress_count - 1];
    g_progress_count--;
    return 0;
}

const char *agent_summary_get(const char *agent_id) {
    int idx = find_progress(agent_id);
    return idx >= 0 ? g_progress[idx].summary : NULL;
}

bool agent_summary_stale(const char *agent_id, int max_age_s) {
    int idx = find_progress(agent_id);
    if (idx < 0)
        return true;
    return (nc_wall_sec() - g_progress[idx].updated_at) > max_age_s;
}

int agent_summary_count(void) {
    return g_progress_count;
}

char *agent_summary_extract_from_output(const char *output) {
    if (!output || !output[0])
        return nc_xstrdup("(working)");
    const char *last_newline = NULL;
    const char *p = output;
    while (*p) {
        if (*p == '\n')
            last_newline = p;
        p++;
    }
    const char *last_line = last_newline ? last_newline + 1 : output;
    while (*last_line == ' ' || *last_line == '\t')
        last_line++;
    if (*last_line == '\0' && last_newline) {
        const char *prev = output;
        const char *scan = output;
        while (scan < last_newline) {
            if (*scan == '\n')
                prev = scan + 1;
            scan++;
        }
        last_line = prev;
    }
    size_t len = strlen(last_line);
    if (len > SUMMARY_MAX_LEN - 1)
        len = SUMMARY_MAX_LEN - 1;
    char *result = malloc(len + 1);
    if (!result)
        return NULL;
    memcpy(result, last_line, len);
    result[len] = '\0';
    char *nl = strchr(result, '\n');
    if (nl)
        *nl = '\0';
    return result;
}

char *agent_summary_format_all(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Agent Progress (%d):\n", g_progress_count);
    long now = nc_wall_sec();
    for (int i = 0; i < g_progress_count; i++) {
        long age = now - g_progress[i].updated_at;
        strbuf_appendf(&sb, "  %s[%s]%s %s (%lds ago, %d turns)\n",
                       agent_color_for(g_progress[i].agent_id), g_progress[i].agent_id, T_RESET,
                       g_progress[i].summary, age, g_progress[i].turn_count);
    }
    return strbuf_detach(&sb);
}
