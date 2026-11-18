/*
 * rollout_metadata.c — Rollout metadata builder
 *
 * Constructs metadata headers for rollout recordings,
 * including session info, model info, and tool stats.
 */

#include "nevercode.h"
#include "defaults.h"

/* ── Build ── */

void rollout_meta_init(rollout_meta_t *m) {
    memset(m, 0, sizeof(*m));
    m->started_at = nc_wall_sec();
}

void rollout_meta_set_session(rollout_meta_t *m, const char *id, const char *model,
                              const char *workspace) {
    if (id)
        nc_strlcpy(m->session_id, id, sizeof(m->session_id));
    if (model)
        nc_strlcpy(m->model, model, sizeof(m->model));
    if (workspace)
        nc_strlcpy(m->workspace, workspace, sizeof(m->workspace));
}

void rollout_meta_set_source(rollout_meta_t *m, session_source_t src) {
    if (m)
        m->source = src;
}

void rollout_meta_set_parent(rollout_meta_t *m, const char *parent_id) {
    if (m && parent_id)
        nc_strlcpy(m->parent_session, parent_id, sizeof(m->parent_session));
}

const char *session_source_name(session_source_t src) {
    switch (src) {
    case SESSION_SRC_CLI:
        return "cli";
    case SESSION_SRC_SERVER:
        return "server";
    case SESSION_SRC_SDK:
        return "sdk";
    case SESSION_SRC_REMOTE:
        return "remote";
    case SESSION_SRC_AGENT:
        return "agent";
    }
    return "cli";
}

session_source_t session_source_parse(const char *name) {
    if (!name)
        return SESSION_SRC_CLI;
    if (strcmp(name, "server") == 0)
        return SESSION_SRC_SERVER;
    if (strcmp(name, "sdk") == 0)
        return SESSION_SRC_SDK;
    if (strcmp(name, "remote") == 0)
        return SESSION_SRC_REMOTE;
    if (strcmp(name, "agent") == 0)
        return SESSION_SRC_AGENT;
    return SESSION_SRC_CLI;
}

void rollout_meta_record_turn(rollout_meta_t *m, int tool_calls, int in_tok, int out_tok,
                              double turn_cost) {
    m->turn_count++;
    m->tool_call_count += tool_calls;
    m->input_tokens += in_tok;
    m->output_tokens += out_tok;
    m->cost += turn_cost;
}

void rollout_meta_record_compact(rollout_meta_t *m) {
    m->compact_count++;
}

void rollout_meta_finalize(rollout_meta_t *m, const char *summary) {
    m->ended_at = nc_wall_sec();
    if (summary)
        nc_strlcpy(m->summary, summary, sizeof(m->summary));
}

/* ── Serialization ── */

char *rollout_meta_serialize(const rollout_meta_t *m) {
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "session_id", m->session_id);
    cJSON_AddStringToObject(obj, "model", m->model);
    cJSON_AddStringToObject(obj, "workspace", m->workspace);
    cJSON_AddStringToObject(obj, "source", session_source_name(m->source));
    if (m->parent_session[0])
        cJSON_AddStringToObject(obj, "parent_session", m->parent_session);
    cJSON_AddNumberToObject(obj, "turns", m->turn_count);
    cJSON_AddNumberToObject(obj, "tool_calls", m->tool_call_count);
    cJSON_AddNumberToObject(obj, "compactions", m->compact_count);
    cJSON_AddNumberToObject(obj, "input_tokens", m->input_tokens);
    cJSON_AddNumberToObject(obj, "output_tokens", m->output_tokens);
    cJSON_AddNumberToObject(obj, "cost", m->cost);
    cJSON_AddNumberToObject(obj, "started_at", m->started_at);
    cJSON_AddNumberToObject(obj, "ended_at", m->ended_at);
    if (m->summary[0])
        cJSON_AddStringToObject(obj, "summary", m->summary);

    return nc_cjson_detach(obj);
}

int rollout_meta_deserialize(rollout_meta_t *m, const char *json) {
    if (!json)
        return -1;
    cJSON *obj = cJSON_Parse(json);
    if (!obj)
        return -1;

    memset(m, 0, sizeof(*m));

    cJSON *j;
    if ((j = cJSON_GetObjectItem(obj, "session_id")) && cJSON_IsString(j))
        nc_strlcpy(m->session_id, j->valuestring, sizeof(m->session_id));
    if ((j = cJSON_GetObjectItem(obj, "model")) && cJSON_IsString(j))
        nc_strlcpy(m->model, j->valuestring, sizeof(m->model));
    if ((j = cJSON_GetObjectItem(obj, "workspace")) && cJSON_IsString(j))
        nc_strlcpy(m->workspace, j->valuestring, sizeof(m->workspace));
    if ((j = cJSON_GetObjectItem(obj, "source")) && cJSON_IsString(j))
        m->source = session_source_parse(j->valuestring);
    if ((j = cJSON_GetObjectItem(obj, "parent_session")) && cJSON_IsString(j))
        nc_strlcpy(m->parent_session, j->valuestring, sizeof(m->parent_session));
    if ((j = cJSON_GetObjectItem(obj, "turns")))
        m->turn_count = (int)j->valuedouble;
    if ((j = cJSON_GetObjectItem(obj, "tool_calls")))
        m->tool_call_count = (int)j->valuedouble;
    if ((j = cJSON_GetObjectItem(obj, "compactions")))
        m->compact_count = (int)j->valuedouble;
    if ((j = cJSON_GetObjectItem(obj, "input_tokens")))
        m->input_tokens = (int)j->valuedouble;
    if ((j = cJSON_GetObjectItem(obj, "output_tokens")))
        m->output_tokens = (int)j->valuedouble;
    if ((j = cJSON_GetObjectItem(obj, "cost")))
        m->cost = j->valuedouble;
    if ((j = cJSON_GetObjectItem(obj, "started_at")))
        m->started_at = (long)j->valuedouble;
    if ((j = cJSON_GetObjectItem(obj, "ended_at")))
        m->ended_at = (long)j->valuedouble;
    if ((j = cJSON_GetObjectItem(obj, "summary")) && cJSON_IsString(j))
        nc_strlcpy(m->summary, j->valuestring, sizeof(m->summary));

    cJSON_Delete(obj);
    return 0;
}

long rollout_meta_duration_s(const rollout_meta_t *m) {
    if (m->ended_at > m->started_at)
        return m->ended_at - m->started_at;
    return nc_wall_sec() - m->started_at;
}

char *rollout_meta_format(const rollout_meta_t *m) {
    size_t sz = 4096; /* session(63)+model(63)+workspace(4095)+summary(256)+labels fit */
    char *buf = malloc(sz);
    if (!buf)
        return NULL;

    snprintf(buf, sz,
             "Session: %.63s\nModel: %.63s\nWorkspace: %.3500s\n"
             "Turns: %d | Tools: %d | Compactions: %d\n"
             "Tokens: %d in / %d out | Cost: $%.4f\n"
             "Duration: %lds\n%s%.250s",
             m->session_id, m->model, m->workspace, m->turn_count, m->tool_call_count,
             m->compact_count, m->input_tokens, m->output_tokens, m->cost,
             rollout_meta_duration_s(m), m->summary[0] ? "Summary: " : "",
             m->summary[0] ? m->summary : "");
    return buf;
}

int rollout_meta_save(const rollout_meta_t *m, const char *session_id) {
    if (!m || !session_id || !session_id[0])
        return -1;

    char dir[NC_PATH_MAX];
    config_home_path(dir, sizeof(dir), "sessions");
    nc_mkdir_p(dir);

    char sess_dir[NC_PATH_MAX];
    snprintf(sess_dir, sizeof(sess_dir), "%.2045s/%.2045s", dir, session_id);
    mkdir(sess_dir, NC_DIR_MODE_PRIVATE);

    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%.4079s/metadata.json", sess_dir);

    char *json = rollout_meta_serialize(m);
    if (!json)
        return -1;

    FILE *f = fopen(path, "w");
    if (!f) {
        free(json);
        return -1;
    }
    fprintf(f, "%s\n", json);
    int rc = fflush(f) ? -1 : 0;
    fclose(f);
    free(json);
    return rc;
}
