/*
 * rollout_replay.c — Session event replay and context reconstruction
 */
#include "nevercode.h"
#include "conversation.h"
#include "harness.h"

/* ROLLOUT_SUBDIR, ROLLOUT_EVENTS_FILENAME in defaults.h */
#define ROLLOUT_DIR ROLLOUT_SUBDIR
#define ROLLOUT_EVENTS_FILE ROLLOUT_EVENTS_FILENAME

typedef enum {
    RE_USER_MSG = 0,
    RE_ASSIST_MSG = 1,
    RE_TOOL_CALL = 2,
    RE_TOOL_RESULT = 3,
    RE_COMPACT = 4,
    RE_HANDOFF = 5,
    RE_BRANCH = 6
} replay_event_kind_t;

typedef struct {
    replay_event_kind_t kind;
    long timestamp;
    int turn_id;
    char *data;
    char *metadata;
} replay_event_t;
typedef struct {
    message_t *msgs;
    int msg_count;
    int msg_cap;
    int last_turn;
} replay_result_t;

static replay_event_kind_t classify_event(const cJSON *ev) {
    const char *s = nc_cjson_str(ev, "type", NULL);
    if (!s)
        return RE_USER_MSG;
    if (strcmp(s, "user") == 0)
        return RE_USER_MSG;
    if (strcmp(s, "assistant") == 0)
        return RE_ASSIST_MSG;
    if (strcmp(s, "tool_call") == 0)
        return RE_TOOL_CALL;
    if (strcmp(s, "tool_result") == 0)
        return RE_TOOL_RESULT;
    if (strcmp(s, "compact") == 0)
        return RE_COMPACT;
    if (strcmp(s, "handoff") == 0)
        return RE_HANDOFF;
    if (strcmp(s, "branch") == 0)
        return RE_BRANCH;
    return RE_USER_MSG;
}

static int parse_events_file(const char *path, replay_event_t *events, int max) {
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char line[REPLAY_LINE_BUF];
    int count = 0;
    while (count < max && fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';
        if (len == 0)
            continue;
        cJSON *ev = cJSON_Parse(line);
        if (!ev)
            continue;
        events[count].kind = classify_event(ev);
        events[count].timestamp = (long)nc_cjson_dbl(ev, "ts", 0);
        events[count].turn_id = nc_cjson_int(ev, "turn", count);
        events[count].data = nc_cjson_dup_str(ev, "content", NULL);
        const cJSON *meta = cJSON_GetObjectItem(ev, "meta");
        events[count].metadata = meta ? cJSON_PrintUnformatted(meta) : NULL;
        cJSON_Delete(ev);
        count++;
    }
    fclose(f);
    return count;
}

static void free_events(replay_event_t *events, int n) {
    for (int i = 0; i < n; i++) {
        free(events[i].data);
        free(events[i].metadata);
    }
}

static int find_last_checkpoint(const replay_event_t *events, int n) {
    for (int i = n - 1; i >= 0; i--)
        if (events[i].kind == RE_COMPACT || events[i].kind == RE_HANDOFF)
            return i;
    return -1;
}

static void result_add_msg(replay_result_t *r, message_role_t role, const char *text) {
    if (r->msg_count >= r->msg_cap) {
        r->msg_cap = r->msg_cap ? r->msg_cap * 2 : 64;
        message_t *tmp = realloc(r->msgs, sizeof(message_t) * r->msg_cap);
        if (!tmp)
            return;
        r->msgs = tmp;
    }
    message_t *m = &r->msgs[r->msg_count];
    memset(m, 0, sizeof(*m));
    m->role = role;
    if (text) {
        m->blocks = calloc(1, sizeof(content_block_t));
        if (!m->blocks)
            return;
        m->blocks[0].type = CONTENT_TEXT;
        m->blocks[0].text = nc_xstrdup(text);
        m->num_blocks = 1;
        m->cap_blocks = 1;
    }
    r->msg_count++;
}

int rollout_replay_from_session(const char *session_id, message_t **out_msgs, int *out_count,
                                char **resume_ctx) {
    if (!session_id || !out_msgs || !out_count)
        return -1;
    char path[NC_PATH_MAX];
    config_home_path(path, sizeof(path), ROLLOUT_DIR);
    char events_path[NC_PATH_MAX];
    snprintf(events_path, sizeof(events_path), "%.1362s/%.1362s/%.1362s", path, session_id, ROLLOUT_EVENTS_FILE);
    replay_event_t *events = calloc(MAX_REPLAY_EVENTS, sizeof(replay_event_t));
    if (!events)
        return -1;
    int event_count = parse_events_file(events_path, events, MAX_REPLAY_EVENTS);
    if (event_count == 0) {
        free(events);
        return -1;
    }
    int checkpoint = find_last_checkpoint(events, event_count);
    int replay_from = (checkpoint >= 0) ? checkpoint : 0;
    replay_result_t result = {0};
    if (checkpoint >= 0 && events[checkpoint].data)
        result_add_msg(&result, ROLE_ASSISTANT, events[checkpoint].data);
    for (int i = replay_from + (checkpoint >= 0 ? 1 : 0); i < event_count; i++) {
        switch (events[i].kind) {
        case RE_USER_MSG:
            result_add_msg(&result, ROLE_USER, events[i].data);
            result.last_turn = events[i].turn_id;
            break;
        case RE_ASSIST_MSG:
            result_add_msg(&result, ROLE_ASSISTANT, events[i].data);
            break;
        default:
            break;
        }
    }
    if (resume_ctx) {
        strbuf_t sb;
        strbuf_init(&sb);
        strbuf_appendf(&sb, "Resumed from session %s (turn %d, %d events replayed)", session_id,
                       result.last_turn, event_count - replay_from);
        if (checkpoint >= 0)
            strbuf_appendf(&sb, ", checkpoint at event %d", checkpoint);
        *resume_ctx = strbuf_detach(&sb);
    }
    free_events(events, event_count);
    free(events);
    *out_msgs = result.msgs;
    *out_count = result.msg_count;
    return 0;
}

int rollout_replay_event_count(const char *session_id) {
    if (!session_id)
        return 0;
    char path[NC_PATH_MAX];
    config_home_path(path, sizeof(path), ROLLOUT_DIR);
    char events_path[NC_PATH_MAX];
    snprintf(events_path, sizeof(events_path), "%.1362s/%.1362s/%.1362s", path, session_id, ROLLOUT_EVENTS_FILE);
    FILE *f = fopen(events_path, "r");
    if (!f)
        return 0;
    int count = 0;
    char line[NC_LINE_MAX];
    while (fgets(line, sizeof(line), f))
        if (line[0] != '\n')
            count++;
    fclose(f);
    return count;
}

int rollout_replay_append_event(const char *session_id, const char *type, int turn,
                                const char *content, const cJSON *meta) {
    if (!session_id || !type)
        return -1;
    char dir[NC_PATH_MAX];
    config_home_path(dir, sizeof(dir), ROLLOUT_DIR);
    char sess_dir[NC_PATH_MAX];
    snprintf(sess_dir, sizeof(sess_dir), "%.2045s/%.2045s", dir, session_id);
    mkdir(dir, NC_DIR_MODE_PRIVATE);
    mkdir(sess_dir, NC_DIR_MODE_PRIVATE);
    char events_path[NC_PATH_MAX];
    snprintf(events_path, sizeof(events_path), "%.2045s/%.2045s", sess_dir, ROLLOUT_EVENTS_FILE);
    cJSON *ev = cJSON_CreateObject();
    cJSON_AddStringToObject(ev, "type", type);
    cJSON_AddNumberToObject(ev, "turn", turn);
    cJSON_AddNumberToObject(ev, "ts", (double)nc_time_now());
    if (content)
        cJSON_AddStringToObject(ev, "content", content);
    if (meta)
        cJSON_AddItemToObject(ev, "meta", cJSON_Duplicate(meta, 1));
    char *line = nc_cjson_detach(ev);
    if (!line)
        return -1;
    FILE *f = fopen(events_path, "a");
    if (!f) {
        free(line);
        return -1;
    }
    fprintf(f, "%s\n", line);
    int rc = fflush(f) ? -1 : 0;
    fclose(f);
    free(line);
    return rc;
}

int rollout_replay_list_sessions(char **ids, int max) {
    char dir[NC_PATH_MAX];
    config_home_path(dir, sizeof(dir), ROLLOUT_DIR);
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    int count = 0;
    struct dirent *ent;
    while (count < max && (ent = readdir(d))) {
        if (ent->d_name[0] == '.')
            continue;
        char check[NC_PATH_MAX];
        snprintf(check, sizeof(check), "%.1362s/%.1362s/%.1362s", dir, ent->d_name, ROLLOUT_EVENTS_FILE);
        struct stat st;
        if (stat(check, &st) == 0)
            ids[count++] = nc_xstrdup(ent->d_name);
    }
    closedir(d);
    return count;
}

char *rollout_replay_describe(const char *session_id) {
    int n = rollout_replay_event_count(session_id);
    if (n == 0)
        return nc_xstrdup("(no events)");
    char buf[NC_LABEL_MAX];
    snprintf(buf, sizeof(buf), "session %s: %d events", session_id, n);
    return nc_xstrdup(buf);
}

/* ── Reverse Scan Reconstruction ──
 * Scans events newest-to-oldest to find the newest surviving checkpoint
 * and any metadata (rollback counts, reference context) needed for
 * session resume/fork.
 */

int rollout_replay_reconstruct(const char *session_id, message_t **out_msgs, int *out_count,
                               char **resume_ctx, char **reference_ctx) {
    if (!session_id || !out_msgs || !out_count)
        return -1;
    char path[NC_PATH_MAX];
    config_home_path(path, sizeof(path), ROLLOUT_DIR);
    char events_path[NC_PATH_MAX];
    snprintf(events_path, sizeof(events_path), "%.1362s/%.1362s/%.1362s", path, session_id, ROLLOUT_EVENTS_FILE);
    replay_event_t *events = calloc(MAX_REPLAY_EVENTS, sizeof(replay_event_t));
    if (!events)
        return -1;
    int event_count = parse_events_file(events_path, events, MAX_REPLAY_EVENTS);
    if (event_count == 0) {
        free(events);
        return -1;
    }

    /* Reverse scan: find newest checkpoint and count pending rollbacks */
    int newest_checkpoint = -1;
    int pending_rollbacks = 0;
    char *last_model = NULL;
    char *last_ref_ctx = NULL;

    for (int i = event_count - 1; i >= 0; i--) {
        if (events[i].kind == RE_COMPACT || events[i].kind == RE_HANDOFF) {
            if (newest_checkpoint < 0)
                newest_checkpoint = i;
            break;
        }
        if (events[i].metadata) {
            cJSON *meta = cJSON_Parse(events[i].metadata);
            if (meta) {
                const cJSON *rb = cJSON_GetObjectItem(meta, "rollback_turns");
                if (rb && cJSON_IsNumber(rb))
                    pending_rollbacks += rb->valueint;
                const cJSON *mdl = cJSON_GetObjectItem(meta, "model");
                if (mdl && cJSON_IsString(mdl) && !last_model)
                    last_model = nc_xstrdup(mdl->valuestring);
                const cJSON *ref = cJSON_GetObjectItem(meta, "reference_context");
                if (ref && cJSON_IsString(ref) && !last_ref_ctx)
                    last_ref_ctx = nc_xstrdup(ref->valuestring);
                cJSON_Delete(meta);
            }
        }
    }

    /* Build history from the checkpoint forward, respecting rollbacks */
    int replay_from = (newest_checkpoint >= 0) ? newest_checkpoint : 0;
    replay_result_t result = {0};

    if (newest_checkpoint >= 0 && events[newest_checkpoint].data)
        result_add_msg(&result, ROLE_ASSISTANT, events[newest_checkpoint].data);

    int user_turns_seen = 0;
    for (int i = replay_from + (newest_checkpoint >= 0 ? 1 : 0); i < event_count; i++) {
        switch (events[i].kind) {
        case RE_USER_MSG:
            user_turns_seen++;
            if (pending_rollbacks > 0 && user_turns_seen <= pending_rollbacks)
                continue;
            result_add_msg(&result, ROLE_USER, events[i].data);
            result.last_turn = events[i].turn_id;
            break;
        case RE_ASSIST_MSG:
            if (pending_rollbacks > 0 && user_turns_seen <= pending_rollbacks)
                continue;
            result_add_msg(&result, ROLE_ASSISTANT, events[i].data);
            break;
        default:
            break;
        }
    }

    if (resume_ctx) {
        strbuf_t sb;
        strbuf_init(&sb);
        strbuf_appendf(&sb, "Resumed session %s (turn %d, %d events, rollbacks=%d)", session_id,
                       result.last_turn, event_count, pending_rollbacks);
        if (newest_checkpoint >= 0)
            strbuf_appendf(&sb, ", checkpoint@%d", newest_checkpoint);
        if (last_model)
            strbuf_appendf(&sb, ", model=%s", last_model);
        *resume_ctx = strbuf_detach(&sb);
    }

    if (reference_ctx)
        *reference_ctx = last_ref_ctx ? nc_xstrdup(last_ref_ctx) : NULL;

    free(last_model);
    free(last_ref_ctx);
    free_events(events, event_count);
    free(events);
    *out_msgs = result.msgs;
    *out_count = result.msg_count;
    return 0;
}

int rollout_replay_drop_last_n_turns(const char *session_id, int n) {
    if (!session_id || n <= 0)
        return -1;
    cJSON *meta = cJSON_CreateObject();
    cJSON_AddNumberToObject(meta, "rollback_turns", n);
    int rc = rollout_replay_append_event(session_id, "rollback", 0, NULL, meta);
    cJSON_Delete(meta);
    return rc;
}

void rollout_replay_free_msgs(message_t *msgs, int n) {
    if (!msgs)
        return;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < msgs[i].num_blocks; j++) {
            free(msgs[i].blocks[j].text);
            free(msgs[i].blocks[j].tool_use_id);
            free(msgs[i].blocks[j].tool_name);
            if (msgs[i].blocks[j].tool_input)
                cJSON_Delete(msgs[i].blocks[j].tool_input);
            free(msgs[i].blocks[j].image_data);
            free(msgs[i].blocks[j].image_media_type);
        }
        free(msgs[i].blocks);
    }
    free(msgs);
}
