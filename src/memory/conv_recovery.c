/*
 * conv_recovery.c — Conversation crash recovery
 *
 * Auto-saves conversation state on SIGTERM/SIGINT/SIGQUIT/crash,
 * and recovers on restart. Keeps a rolling recovery file that's
 * updated after each API round.
 */

#include "nevercode.h"
#include "conversation.h"

/* SESSION_RECOVERY_FILENAME in defaults.h */
#define RECOVERY_FILENAME SESSION_RECOVERY_FILENAME
#define RECOVERY_MAX_AGE RECOVERY_MAX_AGE_S

static char g_recovery_path[NC_PATH_MAX] = {0};
static bool g_recovery_enabled = false;

void conv_recovery_init(void) {
    config_home_path(g_recovery_path, sizeof(g_recovery_path), RECOVERY_FILENAME);
    g_recovery_enabled = true;
}

bool conv_recovery_enabled(void) {
    return g_recovery_enabled;
}

const char *conv_recovery_path(void) {
    return g_recovery_path;
}

int conv_recovery_save(const void *conv_ptr, const char *session_id) {
    const conversation_t *conv = (const conversation_t *)conv_ptr;
    if (!g_recovery_enabled || !conv || conv->len == 0)
        return -1;

    char dir[NC_PATH_MAX];
    config_home_path(dir, sizeof(dir), "");
    mkdir(dir, NC_DIR_MODE_PRIVATE);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", NEVERCODE_VERSION);
    cJSON_AddNumberToObject(root, "timestamp", (double)nc_time_now());
    if (session_id)
        cJSON_AddStringToObject(root, "session_id", session_id);
    cJSON_AddNumberToObject(root, "msg_count", conv->len);

    if (conv->system_prompt)
        cJSON_AddStringToObject(root, "system_prompt", conv->system_prompt);

    cJSON *msgs = cJSON_CreateArray();
    for (int i = 0; i < conv->len; i++) {
        const message_t *m = &conv->msgs[i];
        cJSON *jm = cJSON_CreateObject();

        const char *role = "user";
        if (m->role == ROLE_ASSISTANT)
            role = "assistant";
        else if (m->role == ROLE_TOOL)
            role = "tool";
        else if (m->role == ROLE_SYSTEM)
            role = "system";
        cJSON_AddStringToObject(jm, "role", role);

        cJSON *blocks = cJSON_CreateArray();
        for (int j = 0; j < m->num_blocks; j++) {
            const content_block_t *b = &m->blocks[j];
            cJSON *jb = cJSON_CreateObject();

            switch (b->type) {
            case CONTENT_TEXT:
                cJSON_AddStringToObject(jb, "type", "text");
                if (b->text)
                    cJSON_AddStringToObject(jb, "text", b->text);
                break;
            case CONTENT_TOOL_USE:
                cJSON_AddStringToObject(jb, "type", "tool_use");
                if (b->tool_use_id)
                    cJSON_AddStringToObject(jb, "id", b->tool_use_id);
                if (b->tool_name)
                    cJSON_AddStringToObject(jb, "name", b->tool_name);
                if (b->tool_input)
                    cJSON_AddItemToObject(jb, "input", cJSON_Duplicate(b->tool_input, 1));
                break;
            case CONTENT_TOOL_RESULT:
                cJSON_AddStringToObject(jb, "type", "tool_result");
                if (b->tool_use_id)
                    cJSON_AddStringToObject(jb, "tool_use_id", b->tool_use_id);
                if (b->text)
                    cJSON_AddStringToObject(jb, "content", b->text);
                cJSON_AddBoolToObject(jb, "is_error", b->is_error);
                break;
            case CONTENT_IMAGE:
                cJSON_AddStringToObject(jb, "type", "image");
                break;
            }
            cJSON_AddItemToArray(blocks, jb);
        }
        cJSON_AddItemToObject(jm, "blocks", blocks);
        cJSON_AddItemToArray(msgs, jm);
    }
    cJSON_AddItemToObject(root, "messages", msgs);

    char *json = nc_cjson_detach(root);
    if (!json)
        return -1;

    char tmp_path[NC_PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_recovery_path);
    int rc = nc_write_file_str(tmp_path, json);
    free(json);
    if (rc)
        return rc;
    if (rename(tmp_path, g_recovery_path) != 0) {
        unlink(tmp_path);
        return -1;
    }
    return 0;
}

bool conv_recovery_exists(void) {
    if (!g_recovery_enabled)
        return false;

    struct stat st;
    if (stat(g_recovery_path, &st) != 0)
        return false;

    time_t age = nc_time_now() - st.st_mtime;
    if (age > RECOVERY_MAX_AGE) {
        unlink(g_recovery_path);
        return false;
    }
    return true;
}

int conv_recovery_load(void *conv_ptr, char **session_id_out) {
    if (!g_recovery_enabled || !conv_ptr)
        return -1;

    size_t fsz = 0;
    char *buf = strbuf_read_file(g_recovery_path, &fsz);
    if (!buf || fsz > NC_READ_CAP_RECOVERY) {
        free(buf);
        return -1;
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
        return -1;

    const char *sid = cJSON_GetStringValue(cJSON_GetObjectItem(root, "session_id"));
    if (sid && session_id_out)
        *session_id_out = nc_xstrdup(sid);

    conversation_t *c = (conversation_t *)conv_ptr;

    const char *sys = cJSON_GetStringValue(cJSON_GetObjectItem(root, "system_prompt"));
    if (sys) {
        free(c->system_prompt);
        c->system_prompt = nc_xstrdup(sys);
    }

    cJSON *msgs = cJSON_GetObjectItem(root, "messages");
    int msg_count = cJSON_GetArraySize(msgs);

    for (int i = 0; i < msg_count; i++) {
        cJSON *jm = cJSON_GetArrayItem(msgs, i);
        const char *role_s = cJSON_GetStringValue(cJSON_GetObjectItem(jm, "role"));
        if (!role_s)
            continue;

        message_role_t role = ROLE_USER;
        if (strcmp(role_s, "assistant") == 0)
            role = ROLE_ASSISTANT;
        else if (strcmp(role_s, "tool") == 0)
            role = ROLE_TOOL;
        else if (strcmp(role_s, "system") == 0)
            role = ROLE_SYSTEM;

        cJSON *blocks = cJSON_GetObjectItem(jm, "blocks");
        int nb = cJSON_GetArraySize(blocks);

        message_t *msg = NULL;
        for (int j = 0; j < nb; j++) {
            cJSON *jb = cJSON_GetArrayItem(blocks, j);
            const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(jb, "type"));
            if (!type)
                continue;

            if (strcmp(type, "text") == 0) {
                const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(jb, "text"));
                if (text) {
                    if (role == ROLE_USER) {
                        conversation_add_user_text(c, text);
                    } else {
                        if (!msg)
                            msg = conversation_add(c, role);
                        message_add_text(msg, text);
                    }
                }
            } else if (strcmp(type, "tool_use") == 0) {
                const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(jb, "id"));
                const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(jb, "name"));
                cJSON *input = cJSON_GetObjectItem(jb, "input");
                if (id && name) {
                    if (!msg)
                        msg = conversation_add(c, role);
                    message_add_tool_use(msg, id, name, input);
                }
            } else if (strcmp(type, "tool_result") == 0) {
                const char *tid = cJSON_GetStringValue(cJSON_GetObjectItem(jb, "tool_use_id"));
                const char *content_str = cJSON_GetStringValue(cJSON_GetObjectItem(jb, "content"));
                cJSON *is_err = cJSON_GetObjectItem(jb, "is_error");
                bool err = cJSON_IsTrue(is_err);
                if (tid && content_str) {
                    if (!msg)
                        msg = conversation_add(c, ROLE_TOOL);
                    message_add_tool_result(msg, tid, content_str, err ? 1 : 0);
                }
            }
        }
    }

    cJSON_Delete(root);
    return msg_count;
}

void conv_recovery_clear(void) {
    if (g_recovery_enabled)
        unlink(g_recovery_path);
}

long conv_recovery_age_seconds(void) {
    struct stat st;
    if (stat(g_recovery_path, &st) != 0)
        return -1;
    return (long)(nc_time_now() - st.st_mtime);
}

char *conv_recovery_describe(void) {
    if (!conv_recovery_exists())
        return nc_xstrdup("No recovery file found.\n");

    struct stat st;
    stat(g_recovery_path, &st);
    long age = (long)(nc_time_now() - st.st_mtime);

    char buf[NC_MSG_MAX];
    if (age < 60)
        snprintf(buf, sizeof(buf), "Recovery file: %lld bytes, %lds ago\n", (long long)st.st_size,
                 age);
    else if (age < 3600)
        snprintf(buf, sizeof(buf), "Recovery file: %lld bytes, %ldm ago\n", (long long)st.st_size,
                 age / 60);
    else
        snprintf(buf, sizeof(buf), "Recovery file: %lld bytes, %ldh ago\n", (long long)st.st_size,
                 age / 3600);
    return nc_xstrdup(buf);
}
