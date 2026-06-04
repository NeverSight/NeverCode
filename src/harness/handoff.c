/*
 * handoff.c — Context handoff state management
 *
 * Serializes the current working state into a structured handoff when
 * context approaches exhaustion, then rebuilds with a resume prompt.
 */

#include "harness.h"

void handoff_state_init(handoff_state_t *h) {
    memset(h, 0, sizeof(*h));
}

void handoff_state_free(handoff_state_t *h) {
    free(h->goal_summary);
    free(h->decisions_made);
    free(h->pending_tasks);
    free(h->files_modified);
    free(h->key_findings);
    free(h->errors_encountered);
    free(h->working_branch);
    memset(h, 0, sizeof(*h));
}

char *handoff_state_serialize(const handoff_state_t *h) {
    cJSON *root = cJSON_CreateObject();

    if (h->goal_summary)
        cJSON_AddStringToObject(root, "goal", h->goal_summary);
    if (h->decisions_made)
        cJSON_AddStringToObject(root, "decisions", h->decisions_made);
    if (h->pending_tasks)
        cJSON_AddStringToObject(root, "pending", h->pending_tasks);
    if (h->files_modified)
        cJSON_AddStringToObject(root, "files", h->files_modified);
    if (h->key_findings)
        cJSON_AddStringToObject(root, "findings", h->key_findings);
    if (h->errors_encountered)
        cJSON_AddStringToObject(root, "errors", h->errors_encountered);
    if (h->working_branch)
        cJSON_AddStringToObject(root, "branch", h->working_branch);
    if (h->dirty_files > 0)
        cJSON_AddNumberToObject(root, "dirty_files", h->dirty_files);

    cJSON_AddNumberToObject(root, "turns", h->turn_count);
    cJSON_AddNumberToObject(root, "handoffs", h->handoff_count);
    cJSON_AddNumberToObject(root, "elapsed", h->elapsed_s);

    return nc_cjson_detach(root);
}

int handoff_state_deserialize(handoff_state_t *h, const char *json) {
    if (!json)
        return -1;

    cJSON *root = cJSON_Parse(json);
    if (!root)
        return -1;

    handoff_state_free(h);

    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "goal")) && cJSON_IsString(item))
        h->goal_summary = nc_xstrdup(item->valuestring);
    if ((item = cJSON_GetObjectItem(root, "decisions")) && cJSON_IsString(item))
        h->decisions_made = nc_xstrdup(item->valuestring);
    if ((item = cJSON_GetObjectItem(root, "pending")) && cJSON_IsString(item))
        h->pending_tasks = nc_xstrdup(item->valuestring);
    if ((item = cJSON_GetObjectItem(root, "files")) && cJSON_IsString(item))
        h->files_modified = nc_xstrdup(item->valuestring);
    if ((item = cJSON_GetObjectItem(root, "findings")) && cJSON_IsString(item))
        h->key_findings = nc_xstrdup(item->valuestring);
    if ((item = cJSON_GetObjectItem(root, "errors")) && cJSON_IsString(item))
        h->errors_encountered = nc_xstrdup(item->valuestring);
    if ((item = cJSON_GetObjectItem(root, "branch")) && cJSON_IsString(item))
        h->working_branch = nc_xstrdup(item->valuestring);
    if ((item = cJSON_GetObjectItem(root, "dirty_files")))
        h->dirty_files = (int)cJSON_GetNumberValue(item);
    if ((item = cJSON_GetObjectItem(root, "turns")))
        h->turn_count = (int)cJSON_GetNumberValue(item);
    if ((item = cJSON_GetObjectItem(root, "handoffs")))
        h->handoff_count = (int)cJSON_GetNumberValue(item);
    if ((item = cJSON_GetObjectItem(root, "elapsed")))
        h->elapsed_s = (long)cJSON_GetNumberValue(item);

    cJSON_Delete(root);
    return 0;
}

char *handoff_build_resume_prompt(const handoff_state_t *h) {
    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_str(&sb, "[Context compacted. Session state summary follows. "
                           "Continue from where you left off.]\n\n");

    if (h->goal_summary) {
        strbuf_append_str(&sb, "## Current Goal\n");
        strbuf_append_str(&sb, h->goal_summary);
        strbuf_append_str(&sb, "\n\n");
    }
    if (h->decisions_made) {
        strbuf_append_str(&sb, "## Decisions Made\n");
        strbuf_append_str(&sb, h->decisions_made);
        strbuf_append_str(&sb, "\n\n");
    }
    if (h->pending_tasks) {
        strbuf_append_str(&sb, "## Pending Tasks\n");
        strbuf_append_str(&sb, h->pending_tasks);
        strbuf_append_str(&sb, "\n\n");
    }
    if (h->files_modified) {
        strbuf_append_str(&sb, "## Files Modified\n");
        strbuf_append_str(&sb, h->files_modified);
        strbuf_append_str(&sb, "\n\n");
    }
    if (h->key_findings) {
        strbuf_append_str(&sb, "## Key Findings\n");
        strbuf_append_str(&sb, h->key_findings);
        strbuf_append_str(&sb, "\n\n");
    }
    if (h->errors_encountered) {
        strbuf_append_str(&sb, "## Errors Encountered\n");
        strbuf_append_str(&sb, h->errors_encountered);
        strbuf_append_str(&sb, "\n\n");
    }

    strbuf_appendf(&sb, "Session: %d turns completed, %d handoffs, %lds elapsed.", h->turn_count,
                   h->handoff_count, h->elapsed_s);
    if (h->working_branch)
        strbuf_appendf(&sb, " Branch: %s.", h->working_branch);
    if (h->dirty_files > 0)
        strbuf_appendf(&sb, " Dirty files: %d.", h->dirty_files);
    strbuf_append_str(&sb, "\n");

    return strbuf_detach(&sb);
}

/* ── Checkpoint persistence ── */

int harness_checkpoint_save(const char *session_id, int turn, const handoff_state_t *state) {
    char path[NC_PATH_MAX];
    config_home_path(path, sizeof(path), "checkpoints");

    nc_mkdir_p(path);

    char filepath[NC_PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s.json", path, session_id ? session_id : "default");

    char *json = handoff_state_serialize(state);
    if (!json)
        return -1;

    cJSON *wrapper = cJSON_CreateObject();
    cJSON_AddStringToObject(wrapper, "session_id", session_id ? session_id : "default");
    cJSON_AddNumberToObject(wrapper, "turn", turn);
    cJSON_AddNumberToObject(wrapper, "timestamp", (double)nc_time_now());

    cJSON *state_obj = cJSON_Parse(json);
    free(json);
    if (state_obj)
        cJSON_AddItemToObject(wrapper, "state", state_obj);

    char *output = nc_cjson_detach_pretty(wrapper);
    if (!output)
        return -1;

    int rc = nc_write_file_atomic(filepath, output, strlen(output));
    free(output);
    return rc;
}

int harness_checkpoint_load(const char *session_id, handoff_state_t *state, int *turn) {
    char dir[NC_PATH_MAX];
    config_home_path(dir, sizeof(dir), "checkpoints");

    char filepath[NC_PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s.json", dir, session_id ? session_id : "default");

    size_t n = 0;
    char *buf = strbuf_read_file(filepath, &n);
    if (!buf || n == 0) {
        free(buf);
        return -1;
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
        return -1;

    if (turn) {
        cJSON *t = cJSON_GetObjectItem(root, "turn");
        *turn = t ? (int)cJSON_GetNumberValue(t) : 0;
    }

    cJSON *st = cJSON_GetObjectItem(root, "state");
    if (st) {
        char *sj = cJSON_PrintUnformatted(st);
        handoff_state_deserialize(state, sj);
        free(sj);
    }

    cJSON_Delete(root);
    return 0;
}

int harness_checkpoint_cleanup(const char *session_id, int keep_recent) {
    (void)keep_recent;

    char dir[NC_PATH_MAX];
    config_home_path(dir, sizeof(dir), "checkpoints");

    char filepath[NC_PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s.json", dir, session_id ? session_id : "default");

    return remove(filepath) == 0 ? 0 : -1;
}
