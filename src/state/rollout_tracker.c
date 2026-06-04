/* rollout_tracker.c -- Session rollout tracking for replay/reconstruction. */
#include "nevercode.h"
#include "defaults.h"

typedef enum {
    RK_UMSG,
    RK_AMSG,
    RK_TCALL,
    RK_TRES,
    RK_COMPACT,
    RK_CKPT,
    RK_HANDOFF,
    RK_FORK,
    RK_SET
} rk_t;

static const char *rk_type_str(rk_t k) {
    switch (k) {
    case RK_UMSG:
        return "user";
    case RK_AMSG:
        return "assistant";
    case RK_TCALL:
        return "tool_call";
    case RK_TRES:
        return "tool_result";
    case RK_COMPACT:
        return "compact";
    case RK_CKPT:
        return "checkpoint";
    case RK_HANDOFF:
        return "handoff";
    case RK_FORK:
        return "fork";
    case RK_SET:
        return "settings";
    }
    return "unknown";
}

typedef struct {
    rk_t kind;
    long ts;
    int turn;
    char *data;
} ro_ev_t;

static struct {
    char sid[SESSION_SNAPSHOT_ID_MAX];
    ro_ev_t evs[ROLLOUT_MAX_EVENTS];
    int cnt;
    int cur_turn;
    bool dirty;
    int last_flushed;
} g_ro;

void rollout_tracker_init(const char *sid) {
    memset(&g_ro, 0, sizeof(g_ro));
    if (sid)
        nc_strlcpy(g_ro.sid, sid, sizeof(g_ro.sid));
}

void rollout_tracker_free(void) {
    for (int i = 0; i < g_ro.cnt; i++)
        free(g_ro.evs[i].data);
    memset(&g_ro, 0, sizeof(g_ro));
}

static int add_ev(rk_t k, const char *d) {
    if (g_ro.cnt >= ROLLOUT_MAX_EVENTS)
        return -1;
    ro_ev_t *e = &g_ro.evs[g_ro.cnt];
    e->kind = k;
    e->ts = nc_wall_sec();
    e->turn = g_ro.cur_turn;
    e->data = d ? nc_xstrdup(d) : NULL;
    g_ro.cnt++;
    g_ro.dirty = true;
    return 0;
}

int rollout_record_user_message(const char *t) {
    g_ro.cur_turn++;
    cJSON *o = cJSON_CreateObject();
    if (t)
        cJSON_AddStringToObject(o, "text", t);
    cJSON_AddNumberToObject(o, "turn", g_ro.cur_turn);
    char *j = nc_cjson_detach(o);
    int r = add_ev(RK_UMSG, j);
    free(j);
    return r;
}

int rollout_record_assistant_message(const char *text) {
    cJSON *o = cJSON_CreateObject();
    if (text)
        cJSON_AddStringToObject(o, "text", text);
    char *j = nc_cjson_detach(o);
    int r = add_ev(RK_AMSG, j);
    free(j);
    return r;
}

int rollout_record_tool_call(const char *tool_name, const char *tool_id, const cJSON *input) {
    cJSON *o = cJSON_CreateObject();
    if (tool_name)
        cJSON_AddStringToObject(o, "name", tool_name);
    if (tool_id)
        cJSON_AddStringToObject(o, "id", tool_id);
    if (input)
        cJSON_AddItemToObject(o, "input", cJSON_Duplicate(input, 1));
    char *j = nc_cjson_detach(o);
    int r = add_ev(RK_TCALL, j);
    free(j);
    return r;
}

int rollout_record_tool_result(const char *tool_id, const char *output, bool is_error) {
    cJSON *o = cJSON_CreateObject();
    if (tool_id)
        cJSON_AddStringToObject(o, "id", tool_id);
    if (output) {
        size_t olen = strlen(output);
        if (olen > TOOL_RESULT_TRUNCATE_TO) {
            char *trunc = nc_xstrndup(output, TOOL_RESULT_TRUNCATE_TO);
            if (trunc) {
                cJSON_AddStringToObject(o, "output", trunc);
                free(trunc);
            }
        } else {
            cJSON_AddStringToObject(o, "output", output);
        }
    }
    if (is_error)
        cJSON_AddTrueToObject(o, "error");
    char *j = nc_cjson_detach(o);
    int r = add_ev(RK_TRES, j);
    free(j);
    return r;
}

int rollout_record_compaction(int b, int a) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "before", b);
    cJSON_AddNumberToObject(o, "after", a);
    cJSON_AddNumberToObject(o, "freed", b - a);
    char *j = nc_cjson_detach(o);
    int r = add_ev(RK_COMPACT, j);
    free(j);
    return r;
}

int rollout_record_handoff(const char *reason) {
    cJSON *o = cJSON_CreateObject();
    if (reason)
        cJSON_AddStringToObject(o, "reason", reason);
    char *j = nc_cjson_detach(o);
    int r = add_ev(RK_HANDOFF, j);
    free(j);
    return r;
}

int rollout_record_checkpoint(const char *label) {
    cJSON *o = cJSON_CreateObject();
    if (label)
        cJSON_AddStringToObject(o, "label", label);
    cJSON_AddNumberToObject(o, "turn", g_ro.cur_turn);
    cJSON_AddNumberToObject(o, "events", g_ro.cnt);
    char *j = nc_cjson_detach(o);
    int r = add_ev(RK_CKPT, j);
    free(j);
    return r;
}

int rollout_record_fork(const char *parent_session, int from_turn) {
    cJSON *o = cJSON_CreateObject();
    if (parent_session)
        cJSON_AddStringToObject(o, "parent", parent_session);
    cJSON_AddNumberToObject(o, "from_turn", from_turn);
    char *j = nc_cjson_detach(o);
    int r = add_ev(RK_FORK, j);
    free(j);
    return r;
}

int rollout_record_settings(const char *key, const char *value) {
    cJSON *o = cJSON_CreateObject();
    if (key)
        cJSON_AddStringToObject(o, "key", key);
    if (value)
        cJSON_AddStringToObject(o, "value", value);
    char *j = nc_cjson_detach(o);
    int r = add_ev(RK_SET, j);
    free(j);
    return r;
}

int rollout_event_count(void) {
    return g_ro.cnt;
}

int rollout_current_turn(void) {
    return g_ro.cur_turn;
}

bool rollout_is_dirty(void) {
    return g_ro.dirty;
}

int rollout_save(void) {
    if (!g_ro.dirty || g_ro.sid[0] == '\0')
        return 0;
    if (g_ro.last_flushed >= g_ro.cnt)
        return 0;

    char dir[NC_PATH_MAX];
    config_home_path(dir, sizeof(dir), "sessions");
    nc_mkdir_p(dir);

    char sess_dir[NC_PATH_MAX];
    snprintf(sess_dir, sizeof(sess_dir), "%s/%s", dir, g_ro.sid);
    mkdir(sess_dir, NC_DIR_MODE_PRIVATE);

    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/events.jsonl", sess_dir);
    FILE *f = fopen(path, "a");
    if (!f)
        return -1;

    for (int i = g_ro.last_flushed; i < g_ro.cnt; i++) {
        const ro_ev_t *e = &g_ro.evs[i];
        cJSON *ev = cJSON_CreateObject();
        cJSON_AddStringToObject(ev, "type", rk_type_str(e->kind));
        cJSON_AddNumberToObject(ev, "turn", e->turn);
        cJSON_AddNumberToObject(ev, "ts", (double)e->ts);
        if (e->data)
            cJSON_AddStringToObject(ev, "content", e->data);
        char *line = nc_cjson_detach(ev);
        if (line) {
            fprintf(f, "%s\n", line);
            free(line);
        }
    }

    int rc = fflush(f) ? -1 : 0;
    fclose(f);
    g_ro.last_flushed = g_ro.cnt;
    g_ro.dirty = false;
    return rc;
}
