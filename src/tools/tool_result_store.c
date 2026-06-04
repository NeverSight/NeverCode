/*
 * tool_result_store.c — Persistent tool result storage
 *
 * When tool outputs exceed the inline limit, they are spilled
 * to disk and replaced with a reference. This module manages
 * the spill directory, reading back results on demand.
 */

#include "nevercode.h"

/* TRS_MAX_ENTRIES, TRS_ID_MAX in defaults.h */
#define TRS_REF_FMT "[result stored: %s]"

typedef struct {
    char id[TRS_ID_MAX];
    char path[NC_PATH_MAX];
    size_t size;
    time_t stored_at;
    bool valid;
} trs_entry_t;

typedef struct {
    trs_entry_t entries[TRS_MAX_ENTRIES];
    int count;
    char base_dir[NC_PATH_MAX];
    size_t total_bytes;
} trs_store_t;

static trs_store_t g_trs;

int trs_init(const char *project_root) {
    memset(&g_trs, 0, sizeof(g_trs));
    if (!project_root)
        return -1;
    snprintf(g_trs.base_dir, sizeof(g_trs.base_dir), "%s/%s", project_root, TOOL_RESULT_SPILL_DIR);
    nc_ensure_dir(g_trs.base_dir);
    return 0;
}

static int find_entry(const char *id) {
    for (int i = 0; i < g_trs.count; i++)
        if (g_trs.entries[i].valid && strcmp(g_trs.entries[i].id, id) == 0)
            return i;
    return -1;
}

char *trs_store(const char *call_id, const char *content, size_t len) {
    if (!call_id || !content || len == 0)
        return NULL;

    if (g_trs.count >= TRS_MAX_ENTRIES)
        return NULL;

    trs_entry_t *e = &g_trs.entries[g_trs.count];
    memset(e, 0, sizeof(*e));
    nc_strlcpy(e->id, call_id, sizeof(e->id));
    snprintf(e->path, sizeof(e->path), "%s/%s.txt", g_trs.base_dir, call_id);

    FILE *f = fopen(e->path, "w");
    if (!f)
        return NULL;
    size_t written = fwrite(content, 1, len, f);
    int flush_err = fflush(f);
    fclose(f);
    if (written < len || flush_err)
        return NULL;

    e->size = len;
    e->stored_at = nc_time_now();
    e->valid = true;
    g_trs.count++;
    g_trs.total_bytes += len;

    char ref[NC_LABEL_MAX];
    snprintf(ref, sizeof(ref), TRS_REF_FMT, e->id);
    return nc_xstrdup(ref);
}

char *trs_load(const char *id) {
    int idx = find_entry(id);
    if (idx < 0)
        return NULL;

    trs_entry_t *e = &g_trs.entries[idx];
    FILE *f = fopen(e->path, "r");
    if (!f)
        return NULL;

    char *buf = malloc(e->size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, e->size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

char *trs_maybe_spill(const char *call_id, const char *content) {
    return trs_maybe_spill_ex(call_id, content, false);
}

char *trs_maybe_spill_ex(const char *call_id, const char *content, bool has_delegate) {
    if (!content)
        return NULL;
    size_t len = strlen(content);
    if ((int)len <= TOOL_RESULT_MAX_CHARS)
        return nc_xstrdup(content);

    char *truncated = malloc(TOOL_RESULT_TRUNCATE_TO + 1);
    if (!truncated)
        return nc_xstrdup(content);
    memcpy(truncated, content, TOOL_RESULT_TRUNCATE_TO);
    truncated[TOOL_RESULT_TRUNCATE_TO] = '\0';

    char *ref = trs_store(call_id, content, len);
    if (!ref) {
        return truncated;
    }

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, truncated);

    int lines_total = 0;
    for (size_t i = 0; i < len; i++)
        if (content[i] == '\n')
            lines_total++;

    int lines_kept = 0;
    for (int i = 0; i < TOOL_RESULT_TRUNCATE_TO && truncated[i]; i++)
        if (truncated[i] == '\n')
            lines_kept++;

    strbuf_appendf(&sb, "\n\n...%d lines truncated (%zu bytes total)...\n\n",
                   lines_total - lines_kept, len);
    strbuf_append_str(&sb, ref);

    if (has_delegate) {
        strbuf_append_str(&sb,
                          "\nUse the Agent tool to delegate a sub-agent to process this file "
                          "with Grep and Read (with offset/limit). Avoid reading the full file "
                          "yourself to preserve context.");
    } else {
        strbuf_append_str(&sb, "\nUse Grep to search the full content or Read with offset/limit "
                               "to view specific sections.");
    }

    free(truncated);
    free(ref);
    return strbuf_detach(&sb);
}

int trs_count(void) {
    int n = 0;
    for (int i = 0; i < g_trs.count; i++)
        if (g_trs.entries[i].valid)
            n++;
    return n;
}

size_t trs_total_bytes(void) {
    return g_trs.total_bytes;
}

int trs_check_msg_budget(const char **results, const int *lens, int n, int *over_idx) {
    if (!results || n <= 0)
        return 0;
    long total = 0;
    for (int i = 0; i < n; i++) {
        int l = (lens && lens[i] > 0) ? lens[i] : (results[i] ? (int)strlen(results[i]) : 0);
        total += l;
    }
    if (total <= TOOL_RESULTS_PER_MSG_MAX)
        return 0;
    if (over_idx) {
        int biggest = 0;
        int biggest_len = 0;
        for (int i = 0; i < n; i++) {
            int l = (lens && lens[i] > 0) ? lens[i] : (results[i] ? (int)strlen(results[i]) : 0);
            if (l > biggest_len) {
                biggest_len = l;
                biggest = i;
            }
        }
        *over_idx = biggest;
    }
    return 1;
}

int trs_cleanup(int max_age_s) {
    time_t now = nc_time_now();
    int removed = 0;
    for (int i = 0; i < g_trs.count; i++) {
        trs_entry_t *e = &g_trs.entries[i];
        if (!e->valid)
            continue;
        if (difftime(now, e->stored_at) > max_age_s) {
            remove(e->path);
            g_trs.total_bytes -= e->size;
            e->valid = false;
            removed++;
        }
    }
    return removed;
}
