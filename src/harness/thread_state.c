/*
 * thread_state.c — in-memory thread snapshots with JSON save/load
 */

#include "nevercode.h"
#include "defaults.h"

/* TS_MAX_SNAPSHOTS in defaults.h */
#define TS_MAX TS_MAX_SNAPSHOTS
#define TS_FILE TS_STATE_FILE

typedef struct {
    char *thread_id;
    char *session_id;
    int turn;
    int tool_calls;
    int token_usage;
    char *phase;
    char *note;
    long captured_at;
} ts_entry_t;

static struct {
    ts_entry_t entries[TS_MAX];
    int count;
    int inited;
} g_ts;

static void entry_free(ts_entry_t *e) {
    free(e->thread_id);
    free(e->session_id);
    free(e->phase);
    free(e->note);
    memset(e, 0, sizeof(*e));
}

void thread_state_init(void) {
    if (g_ts.inited)
        return;
    memset(&g_ts, 0, sizeof(g_ts));
    g_ts.inited = 1;
}

void thread_state_free(void) {
    for (int i = 0; i < g_ts.count; i++)
        entry_free(&g_ts.entries[i]);
    g_ts.count = 0;
    g_ts.inited = 0;
}

static int find_idx(const char *thread_id) {
    if (!thread_id)
        return -1;
    for (int i = 0; i < g_ts.count; i++) {
        if (g_ts.entries[i].thread_id && strcmp(g_ts.entries[i].thread_id, thread_id) == 0)
            return i;
    }
    return -1;
}

int thread_state_capture(const char *thread_id, const char *session_id, int turn, int tool_calls,
                         int token_usage, const char *phase) {
    thread_state_init();
    if (!thread_id || !thread_id[0])
        return -1;
    int i = find_idx(thread_id);
    if (i < 0) {
        if (g_ts.count >= TS_MAX)
            return -1;
        i = g_ts.count++;
        memset(&g_ts.entries[i], 0, sizeof(ts_entry_t));
        g_ts.entries[i].thread_id = nc_xstrdup(thread_id);
        if (!g_ts.entries[i].thread_id) {
            g_ts.count--;
            return -1;
        }
    }
    ts_entry_t *e = &g_ts.entries[i];
    free(e->session_id);
    e->session_id = session_id && session_id[0] ? nc_xstrdup(session_id) : NULL;
    e->turn = turn;
    e->tool_calls = tool_calls;
    e->token_usage = token_usage;
    free(e->phase);
    e->phase = phase && phase[0] ? nc_xstrdup(phase) : NULL;
    e->captured_at = nc_wall_sec();
    return i;
}

int thread_state_annotate(int idx, const char *note) {
    if (idx < 0 || idx >= g_ts.count)
        return -1;
    free(g_ts.entries[idx].note);
    g_ts.entries[idx].note = note && note[0] ? nc_xstrdup(note) : NULL;
    return 0;
}

int thread_state_save(const char *dir) {
    if (!dir || !dir[0])
        return -1;
    thread_state_init();
    if (nc_mkdir_p(dir) != 0)
        return -1;
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, TS_FILE);
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    if (!root || !arr) {
        cJSON_Delete(root);
        cJSON_Delete(arr);
        return -1;
    }
    for (int i = 0; i < g_ts.count; i++) {
        ts_entry_t *e = &g_ts.entries[i];
        cJSON *o = cJSON_CreateObject();
        if (!o)
            continue;
        cJSON_AddStringToObject(o, "thread_id", e->thread_id ? e->thread_id : "");
        if (e->session_id)
            cJSON_AddStringToObject(o, "session_id", e->session_id);
        cJSON_AddNumberToObject(o, "turn", e->turn);
        cJSON_AddNumberToObject(o, "tool_calls", e->tool_calls);
        cJSON_AddNumberToObject(o, "token_usage", e->token_usage);
        if (e->phase)
            cJSON_AddStringToObject(o, "phase", e->phase);
        if (e->note)
            cJSON_AddStringToObject(o, "note", e->note);
        cJSON_AddNumberToObject(o, "captured_at", (double)e->captured_at);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddItemToObject(root, "threads", arr);
    char *js = nc_cjson_detach_pretty(root);
    if (!js)
        return -1;
    int rc = nc_write_file_atomic(path, js, strlen(js));
    free(js);
    return rc;
}

int thread_state_load(const char *dir) {
    if (!dir || !dir[0])
        return -1;
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", dir, TS_FILE);
    char *buf = strbuf_read_file_max(path, NULL, NC_READ_CAP_THREAD_STATE);
    if (!buf)
        return -1;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
        return -1;
    cJSON *arr = cJSON_GetObjectItem(root, "threads");
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return -1;
    }
    thread_state_free();
    thread_state_init();
    int items = cJSON_GetArraySize(arr);
    for (int i = 0; i < items && g_ts.count < TS_MAX; i++) {
        cJSON *o = cJSON_GetArrayItem(arr, i);
        if (!o)
            continue;
        const char *tid = cJSON_GetStringValue(cJSON_GetObjectItem(o, "thread_id"));
        if (!tid || !tid[0])
            continue;
        ts_entry_t *e = &g_ts.entries[g_ts.count];
        memset(e, 0, sizeof(*e));
        e->thread_id = nc_xstrdup(tid);
        const char *sid = cJSON_GetStringValue(cJSON_GetObjectItem(o, "session_id"));
        if (sid)
            e->session_id = nc_xstrdup(sid);
        cJSON *t = cJSON_GetObjectItem(o, "turn");
        e->turn = (t && cJSON_IsNumber(t)) ? t->valueint : 0;
        cJSON *tc = cJSON_GetObjectItem(o, "tool_calls");
        e->tool_calls = (tc && cJSON_IsNumber(tc)) ? tc->valueint : 0;
        cJSON *tu = cJSON_GetObjectItem(o, "token_usage");
        e->token_usage = (tu && cJSON_IsNumber(tu)) ? tu->valueint : 0;
        const char *ph = cJSON_GetStringValue(cJSON_GetObjectItem(o, "phase"));
        if (ph)
            e->phase = nc_xstrdup(ph);
        const char *nt = cJSON_GetStringValue(cJSON_GetObjectItem(o, "note"));
        if (nt)
            e->note = nc_xstrdup(nt);
        cJSON *ca = cJSON_GetObjectItem(o, "captured_at");
        e->captured_at = (ca && cJSON_IsNumber(ca)) ? (long)ca->valuedouble : nc_wall_sec();
        if (e->thread_id)
            g_ts.count++;
        else
            entry_free(e);
    }
    cJSON_Delete(root);
    return 0;
}

int thread_state_find(const char *thread_id) {
    return find_idx(thread_id);
}

int thread_state_count(void) {
    thread_state_init();
    return g_ts.count;
}

char *thread_state_format(int idx) {
    if (idx < 0 || idx >= g_ts.count)
        return NULL;
    ts_entry_t *e = &g_ts.entries[idx];
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "[%s] session=%s turn=%d tools=%d tokens=%d at=%ld", e->thread_id,
                   e->session_id ? e->session_id : "-", e->turn, e->tool_calls, e->token_usage,
                   e->captured_at);
    if (e->phase) {
        strbuf_append_str(&sb, " phase=");
        strbuf_append_str(&sb, e->phase);
    }
    if (e->note) {
        strbuf_append_str(&sb, " note=");
        strbuf_append_str(&sb, e->note);
    }
    return strbuf_detach(&sb);
}

char *thread_state_format_all(void) {
    thread_state_init();
    strbuf_t sb;
    strbuf_init(&sb);
    if (g_ts.count == 0) {
        strbuf_append_str(&sb, "(no thread snapshots)");
        return strbuf_detach(&sb);
    }
    for (int i = 0; i < g_ts.count; i++) {
        char *ln = thread_state_format(i);
        if (ln) {
            strbuf_append_str(&sb, ln);
            strbuf_append_str(&sb, "\n");
            free(ln);
        }
    }
    return strbuf_detach(&sb);
}
