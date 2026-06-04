/*
 * rollout_index.c — Session index for rollout management
 *
 * Maintains an index of sessions by workspace, date, and status.
 * Supports fast lookup, listing, and archival of sessions.
 */

#include "nevercode.h"
#include "defaults.h"

/* ── Types ── */

typedef struct {
    char session_id[PROTO_ID_LEN];
    char workspace[NC_PATH_MAX];
    char model[MODEL_NAME_MAX];
    long created_at;
    long last_active;
    int turn_count;
    double cost;
    bool archived;
    bool has_memories;
} session_index_entry_t;

typedef struct {
    session_index_entry_t entries[STATE_DB_MAX_SESSIONS];
    int count;
    bool initialized;
} session_index_t;

static session_index_t g_idx;

/* ── Init / Free ── */

void session_index_init(void) {
    memset(&g_idx, 0, sizeof(g_idx));
    g_idx.initialized = true;
}

void session_index_free(void) {
    g_idx.count = 0;
    g_idx.initialized = false;
}

/* ── Registration ── */

int session_index_add(const char *session_id, const char *workspace, const char *model) {
    if (!g_idx.initialized || g_idx.count >= STATE_DB_MAX_SESSIONS)
        return -1;
    if (!session_id)
        return -1;

    /* Check duplicate */
    for (int i = 0; i < g_idx.count; i++)
        if (strcmp(g_idx.entries[i].session_id, session_id) == 0)
            return -1;

    session_index_entry_t *e = &g_idx.entries[g_idx.count++];
    nc_strlcpy(e->session_id, session_id, sizeof(e->session_id));
    if (workspace)
        nc_strlcpy(e->workspace, workspace, sizeof(e->workspace));
    if (model)
        nc_strlcpy(e->model, model, sizeof(e->model));
    e->created_at = nc_wall_sec();
    e->last_active = e->created_at;
    return 0;
}

/* ── Update ── */

int session_index_update_activity(const char *session_id, int turns, double cost) {
    if (!session_id)
        return -1;
    for (int i = 0; i < g_idx.count; i++) {
        if (strcmp(g_idx.entries[i].session_id, session_id) == 0) {
            g_idx.entries[i].last_active = nc_wall_sec();
            g_idx.entries[i].turn_count = turns;
            g_idx.entries[i].cost = cost;
            return 0;
        }
    }
    return -1;
}

int session_index_set_archived(const char *session_id, bool archived) {
    if (!session_id)
        return -1;
    for (int i = 0; i < g_idx.count; i++) {
        if (strcmp(g_idx.entries[i].session_id, session_id) == 0) {
            g_idx.entries[i].archived = archived;
            return 0;
        }
    }
    return -1;
}

int session_index_set_memories(const char *session_id, bool has_memories) {
    if (!session_id)
        return -1;
    for (int i = 0; i < g_idx.count; i++) {
        if (strcmp(g_idx.entries[i].session_id, session_id) == 0) {
            g_idx.entries[i].has_memories = has_memories;
            return 0;
        }
    }
    return -1;
}

/* ── Queries ── */

int session_index_count(void) {
    return g_idx.count;
}

int session_index_active_count(void) {
    int c = 0;
    for (int i = 0; i < g_idx.count; i++)
        if (!g_idx.entries[i].archived)
            c++;
    return c;
}

const session_index_entry_t *session_index_find(const char *session_id) {
    if (!session_id)
        return NULL;
    for (int i = 0; i < g_idx.count; i++)
        if (strcmp(g_idx.entries[i].session_id, session_id) == 0)
            return &g_idx.entries[i];
    return NULL;
}

int session_index_find_by_workspace(const char *workspace, const session_index_entry_t **out,
                                    int max) {
    if (!workspace || !out)
        return 0;
    int c = 0;
    for (int i = 0; i < g_idx.count && c < max; i++) {
        if (strcmp(g_idx.entries[i].workspace, workspace) == 0)
            out[c++] = &g_idx.entries[i];
    }
    return c;
}

/* ── Serialization ── */

char *session_index_serialize(void) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < g_idx.count; i++) {
        const session_index_entry_t *e = &g_idx.entries[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "id", e->session_id);
        cJSON_AddStringToObject(obj, "workspace", e->workspace);
        cJSON_AddStringToObject(obj, "model", e->model);
        cJSON_AddNumberToObject(obj, "created_at", e->created_at);
        cJSON_AddNumberToObject(obj, "last_active", e->last_active);
        cJSON_AddNumberToObject(obj, "turns", e->turn_count);
        cJSON_AddNumberToObject(obj, "cost", e->cost);
        cJSON_AddBoolToObject(obj, "archived", e->archived);
        cJSON_AddBoolToObject(obj, "has_memories", e->has_memories);
        cJSON_AddItemToArray(arr, obj);
    }
    char *out = nc_cjson_detach(arr);
    return out;
}

int session_index_deserialize(const char *json) {
    if (!json)
        return -1;
    cJSON *arr = cJSON_Parse(json);
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        return -1;
    }

    g_idx.count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        if (g_idx.count >= STATE_DB_MAX_SESSIONS)
            break;
        session_index_entry_t *e = &g_idx.entries[g_idx.count];

        cJSON *jid = cJSON_GetObjectItem(item, "id");
        if (!jid || !cJSON_IsString(jid))
            continue;
        nc_strlcpy(e->session_id, jid->valuestring, sizeof(e->session_id));

        cJSON *jws = cJSON_GetObjectItem(item, "workspace");
        if (jws && cJSON_IsString(jws))
            nc_strlcpy(e->workspace, jws->valuestring, sizeof(e->workspace));
        cJSON *jmod = cJSON_GetObjectItem(item, "model");
        if (jmod && cJSON_IsString(jmod))
            nc_strlcpy(e->model, jmod->valuestring, sizeof(e->model));
        cJSON *jca = cJSON_GetObjectItem(item, "created_at");
        if (jca)
            e->created_at = (long)jca->valuedouble;
        cJSON *jla = cJSON_GetObjectItem(item, "last_active");
        if (jla)
            e->last_active = (long)jla->valuedouble;
        cJSON *jt = cJSON_GetObjectItem(item, "turns");
        if (jt)
            e->turn_count = (int)jt->valuedouble;
        cJSON *jc = cJSON_GetObjectItem(item, "cost");
        if (jc)
            e->cost = jc->valuedouble;
        cJSON *jar = cJSON_GetObjectItem(item, "archived");
        e->archived = jar && cJSON_IsTrue(jar);
        cJSON *jmem = cJSON_GetObjectItem(item, "has_memories");
        e->has_memories = jmem && cJSON_IsTrue(jmem);

        g_idx.count++;
    }

    cJSON_Delete(arr);
    return g_idx.count;
}

char *session_index_format(void) {
    if (g_idx.count == 0)
        return nc_xstrdup("No sessions indexed.\n");

    size_t sz = (size_t)g_idx.count * 200 + 128;
    char *buf = malloc(sz);
    if (!buf)
        return NULL;

    int off = snprintf(buf, sz, "Session index (%d total, %d active):\n", g_idx.count,
                       session_index_active_count());
    for (int i = 0; i < g_idx.count; i++) {
        const session_index_entry_t *e = &g_idx.entries[i];
        off += snprintf(buf + off, sz - (size_t)off, "  %s [%s] turns=%d cost=%.4f%s%s\n",
                        e->session_id, e->model, e->turn_count, e->cost,
                        e->archived ? " (archived)" : "", e->has_memories ? " (mem)" : "");
    }
    return buf;
}
