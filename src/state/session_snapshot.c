/*
 * session_snapshot.c — Save and restore full session state.
 *
 * Provides in-memory session snapshots keyed by session ID.
 * Each snapshot captures the conversation, model, token counts,
 * context percentage, and optional goal string.
 */

#include "nevercode.h"
#include "conversation.h"
#include "defaults.h"

typedef struct {
    char session_id[SESSION_SNAPSHOT_ID_MAX];
    char model[MODEL_NAME_MAX];
    long timestamp;
    int turn_number;
    int total_in;
    int total_out;
    int ctx_pct;
    char *goal;
    char *conv_json;
} snap_t;

static snap_t g_snaps[SESSION_SNAPSHOT_MAX];
static int g_snap_count;

/* ── Save ── */

int session_snapshot_save(const char *sid, const void *cv, const char *model, int it, int ot,
                          int cp, const char *goal) {
    const conversation_t *conv = (const conversation_t *)cv;
    if (!sid || !conv)
        return -1;

    /* Find existing or allocate new slot */
    snap_t *s = NULL;
    for (int i = 0; i < g_snap_count; i++)
        if (strcmp(g_snaps[i].session_id, sid) == 0) {
            s = &g_snaps[i];
            break;
        }
    if (!s) {
        if (g_snap_count >= SESSION_SNAPSHOT_MAX)
            return -1;
        s = &g_snaps[g_snap_count++];
        memset(s, 0, sizeof(*s));
    }

    nc_strlcpy(s->session_id, sid, sizeof(s->session_id));
    if (model)
        nc_strlcpy(s->model, model, sizeof(s->model));
    s->timestamp = nc_wall_sec();
    s->turn_number = conv->len;
    s->total_in = it;
    s->total_out = ot;
    s->ctx_pct = cp;

    free(s->goal);
    s->goal = goal ? nc_xstrdup(goal) : NULL;

    /* Serialize conversation to JSON */
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < conv->len; i++) {
        const message_t *m = &conv->msgs[i];
        cJSON *msg = cJSON_CreateObject();

        const char *rn[] = {"system", "user", "assistant", "tool"};
        int ri = (int)m->role;
        if (ri < 0 || ri > 3)
            ri = 1;
        cJSON_AddStringToObject(msg, "role", rn[ri]);

        cJSON *blks = cJSON_CreateArray();
        for (int j = 0; j < m->num_blocks; j++) {
            cJSON *b = cJSON_CreateObject();
            const char *tn[] = {"text", "tool_use", "tool_result", "image"};
            int ti = (int)m->blocks[j].type;
            if (ti < 0 || ti > 3)
                ti = 0;
            cJSON_AddStringToObject(b, "type", tn[ti]);

            if (m->blocks[j].text)
                cJSON_AddStringToObject(b, "text", m->blocks[j].text);
            if (m->blocks[j].tool_use_id)
                cJSON_AddStringToObject(b, "tool_use_id", m->blocks[j].tool_use_id);
            if (m->blocks[j].tool_name)
                cJSON_AddStringToObject(b, "tool_name", m->blocks[j].tool_name);
            cJSON_AddItemToArray(blks, b);
        }
        cJSON_AddItemToObject(msg, "content", blks);
        cJSON_AddItemToArray(arr, msg);
    }

    free(s->conv_json);
    s->conv_json = nc_cjson_detach(arr);
    return 0;
}

/* ── Load ── */

int session_snapshot_load(const char *sid, void *cv) {
    conversation_t *conv = (conversation_t *)cv;
    if (!sid || !conv)
        return -1;

    for (int i = 0; i < g_snap_count; i++) {
        if (strcmp(g_snaps[i].session_id, sid) != 0)
            continue;
        if (!g_snaps[i].conv_json)
            return -1;

        cJSON *arr = cJSON_Parse(g_snaps[i].conv_json);
        if (!cJSON_IsArray(arr)) {
            cJSON_Delete(arr);
            return -1;
        }

        conversation_free(conv);
        conversation_init(conv);

        int cnt = cJSON_GetArraySize(arr);
        for (int j = 0; j < cnt; j++) {
            cJSON *msg = cJSON_GetArrayItem(arr, j);
            const char *rs = cJSON_GetStringValue(cJSON_GetObjectItem(msg, "role"));

            message_role_t role = ROLE_USER;
            if (rs) {
                if (strcmp(rs, "assistant") == 0)
                    role = ROLE_ASSISTANT;
                else if (strcmp(rs, "tool") == 0)
                    role = ROLE_TOOL;
                else if (strcmp(rs, "system") == 0)
                    role = ROLE_SYSTEM;
            }

            message_t *m = conversation_add(conv, role);
            cJSON *ct = cJSON_GetObjectItem(msg, "content");
            int nb = cJSON_GetArraySize(ct);

            for (int k = 0; k < nb; k++) {
                cJSON *blk = cJSON_GetArrayItem(ct, k);
                const char *ts = cJSON_GetStringValue(cJSON_GetObjectItem(blk, "type"));
                const char *txt = cJSON_GetStringValue(cJSON_GetObjectItem(blk, "text"));

                if (ts && strcmp(ts, "text") == 0 && txt) {
                    message_add_text(m, txt);
                } else if (ts && strcmp(ts, "tool_use") == 0) {
                    message_add_tool_use(
                        m, cJSON_GetStringValue(cJSON_GetObjectItem(blk, "tool_use_id")),
                        cJSON_GetStringValue(cJSON_GetObjectItem(blk, "tool_name")),
                        cJSON_GetObjectItem(blk, "tool_input"));
                } else if (ts && strcmp(ts, "tool_result") == 0) {
                    message_add_tool_result(
                        m, cJSON_GetStringValue(cJSON_GetObjectItem(blk, "tool_use_id")),
                        txt ? txt : "", false);
                }
            }
        }
        cJSON_Delete(arr);
        return cnt;
    }
    return -1;
}

/* ── Query ── */

int session_snapshot_count(void) {
    return g_snap_count;
}

void session_snapshot_free_all(void) {
    for (int i = 0; i < g_snap_count; i++) {
        free(g_snaps[i].goal);
        free(g_snaps[i].conv_json);
    }
    g_snap_count = 0;
}
