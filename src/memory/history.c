#include "conversation.h"
#include "nevercode.h"
#include "git_ops.h"

static char *history_dir(void) {
    char path[NC_PATH_MAX];
    config_find_path(path, sizeof(path), "history");
    return nc_xstrdup(path);
}

static int history_session_path(char *out, size_t cap, const char *session_id) {
    char *dir = history_dir();
    if (!dir)
        return -1;
    snprintf(out, cap, "%s/%s.json", dir, session_id);
    free(dir);
    return 0;
}

static cJSON *history_load_root_file(const char *session_id, char *path, size_t cap) {
    if (history_session_path(path, cap, session_id) != 0)
        return NULL;
    return nc_json_load(path);
}

static int history_write_root_file(const char *path, cJSON *root) {
    char *json = cJSON_Print(root);
    if (!json)
        return -1;
    int rc = nc_write_file_str(path, json);
    free(json);
    return rc;
}

static cJSON *message_to_json(const message_t *m) {
    cJSON *jm = cJSON_CreateObject();
    const char *roles[] = {"system", "user", "assistant", "tool"};
    cJSON_AddStringToObject(jm, "role", roles[m->role]);

    cJSON *blocks = cJSON_CreateArray();
    for (int i = 0; i < m->num_blocks; i++) {
        const content_block_t *b = &m->blocks[i];
        cJSON *jb = cJSON_CreateObject();
        switch (b->type) {
        case CONTENT_TEXT:
            cJSON_AddStringToObject(jb, "type", "text");
            cJSON_AddStringToObject(jb, "text", b->text ? b->text : "");
            break;
        case CONTENT_TOOL_USE:
            cJSON_AddStringToObject(jb, "type", "tool_use");
            cJSON_AddStringToObject(jb, "id", b->tool_use_id);
            cJSON_AddStringToObject(jb, "name", b->tool_name);
            if (b->tool_input)
                cJSON_AddItemToObject(jb, "input", cJSON_Duplicate(b->tool_input, 1));
            break;
        case CONTENT_TOOL_RESULT:
            cJSON_AddStringToObject(jb, "type", "tool_result");
            cJSON_AddStringToObject(jb, "id", b->tool_use_id);
            cJSON_AddStringToObject(jb, "text", b->text ? b->text : "");
            if (b->is_error)
                cJSON_AddTrueToObject(jb, "is_error");
            break;
        case CONTENT_IMAGE:
            cJSON_AddStringToObject(jb, "type", "image");
            if (b->image_media_type)
                cJSON_AddStringToObject(jb, "media_type", b->image_media_type);
            break;
        }
        cJSON_AddItemToArray(blocks, jb);
    }
    cJSON_AddItemToObject(jm, "blocks", blocks);
    return jm;
}

int history_save(const conversation_t *c, const char *session_id) {
    char *dir = history_dir();
    if (!dir)
        return -1;
    nc_ensure_dir(dir);

    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s.json", dir, session_id);
    free(dir);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id);
    cJSON_AddNumberToObject(root, "timestamp", (double)nc_time_now());
    if (c->system_prompt)
        cJSON_AddStringToObject(root, "system_prompt", c->system_prompt);

    cJSON *msgs = cJSON_CreateArray();
    for (int i = 0; i < c->len; i++)
        cJSON_AddItemToArray(msgs, message_to_json(&c->msgs[i]));
    cJSON_AddItemToObject(root, "messages", msgs);

    char *json = nc_cjson_detach_pretty(root);
    int rc = nc_write_file_str(path, json);
    free(json);
    return rc;
}

static void json_to_message(const cJSON *jm, message_t *m) {
    const char *role = cJSON_GetStringValue(cJSON_GetObjectItem(jm, "role"));
    message_role_t r = ROLE_USER;
    if (role) {
        if (strcmp(role, "assistant") == 0)
            r = ROLE_ASSISTANT;
        else if (strcmp(role, "tool") == 0)
            r = ROLE_TOOL;
        else if (strcmp(role, "system") == 0)
            r = ROLE_SYSTEM;
    }
    message_init(m, r);

    cJSON *blocks = cJSON_GetObjectItem(jm, "blocks");
    int nb = cJSON_GetArraySize(blocks);
    for (int i = 0; i < nb; i++) {
        cJSON *jb = cJSON_GetArrayItem(blocks, i);
        const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(jb, "type"));
        if (!type)
            continue;

        if (strcmp(type, "text") == 0) {
            message_add_text(m, cJSON_GetStringValue(cJSON_GetObjectItem(jb, "text")));
        } else if (strcmp(type, "tool_use") == 0) {
            message_add_tool_use(m, cJSON_GetStringValue(cJSON_GetObjectItem(jb, "id")),
                                 cJSON_GetStringValue(cJSON_GetObjectItem(jb, "name")),
                                 cJSON_GetObjectItem(jb, "input"));
        } else if (strcmp(type, "tool_result") == 0) {
            message_add_tool_result(m, cJSON_GetStringValue(cJSON_GetObjectItem(jb, "id")),
                                    cJSON_GetStringValue(cJSON_GetObjectItem(jb, "text")),
                                    cJSON_IsTrue(cJSON_GetObjectItem(jb, "is_error")));
        }
    }
}

int history_load(conversation_t *c, const char *session_id) {
    char *dir = history_dir();
    if (!dir)
        return -1;

    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s.json", dir, session_id);
    free(dir);

    char *buf = strbuf_read_file(path, NULL);
    if (!buf)
        return -1;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
        return -1;

    const char *sp = cJSON_GetStringValue(cJSON_GetObjectItem(root, "system_prompt"));
    if (sp) {
        free(c->system_prompt);
        c->system_prompt = nc_xstrdup(sp);
    }

    cJSON *msgs = cJSON_GetObjectItem(root, "messages");
    int nm = cJSON_GetArraySize(msgs);
    for (int i = 0; i < nm; i++) {
        message_t *m = conversation_add(c, ROLE_USER); /* role overwritten below */
        json_to_message(cJSON_GetArrayItem(msgs, i), m);
    }
    /* json_to_message re-inits with the correct role from JSON;
       conversation_add's ROLE_USER is just a placeholder. */

    cJSON_Delete(root);
    return 0;
}

int history_list(char **out) {
    char *dir = history_dir();
    if (!dir) {
        *out = nc_xstrdup("(no history directory)");
        return 0;
    }

    DIR *d = opendir(dir);
    if (!d) {
        free(dir);
        *out = nc_xstrdup("(no sessions found)");
        return 0;
    }

    typedef struct {
        char id[NC_FMT_MAX];
        double ts;
        char preview[NC_TAG_MAX];
    } entry_t;
    entry_t *entries = NULL;
    int count = 0, cap = 0;

    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (!str_has_suffix(ent->d_name, ".json"))
            continue;

        char path[NC_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        char *buf = strbuf_read_file(path, NULL);
        if (!buf)
            continue;

        cJSON *root = cJSON_Parse(buf);
        free(buf);
        if (!root)
            continue;

        if (count >= cap) {
            cap = cap ? cap * 2 : 16;
            entry_t *tmp = realloc(entries, cap * sizeof(entry_t));
            if (!tmp)
                break;
            entries = tmp;
        }
        entry_t *e = &entries[count++];
        const char *sid = cJSON_GetStringValue(cJSON_GetObjectItem(root, "session_id"));
        nc_strlcpy(e->id, sid ? sid : "?", sizeof(e->id));
        cJSON *tsj = cJSON_GetObjectItem(root, "timestamp");
        e->ts = tsj ? tsj->valuedouble : 0;
        e->preview[0] = '\0';

        const char *sname = cJSON_GetStringValue(cJSON_GetObjectItem(root, "name"));
        if (sname && sname[0])
            snprintf(e->preview, sizeof(e->preview), "[%s] ", sname);

        cJSON *msgs = cJSON_GetObjectItem(root, "messages");
        int nm = cJSON_GetArraySize(msgs);
        for (int i = 0; i < nm; i++) {
            cJSON *m = cJSON_GetArrayItem(msgs, i);
            const char *role = cJSON_GetStringValue(cJSON_GetObjectItem(m, "role"));
            if (role && strcmp(role, "user") == 0) {
                cJSON *blocks = cJSON_GetObjectItem(m, "blocks");
                cJSON *b0 = cJSON_GetArrayItem(blocks, 0);
                if (b0) {
                    const char *txt = cJSON_GetStringValue(cJSON_GetObjectItem(b0, "text"));
                    if (txt) {
                        size_t plen = strlen(e->preview);
                        snprintf(e->preview + plen, sizeof(e->preview) - plen, "%.100s", txt);
                    }
                }
                break;
            }
        }
        cJSON_Delete(root);
    }
    closedir(d);
    free(dir);

    for (int i = 0; i < count - 1; i++)
        for (int j = i + 1; j < count; j++)
            if (entries[j].ts > entries[i].ts) {
                entry_t tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }

    strbuf_t sb;
    strbuf_init(&sb);
    int show = count > 20 ? 20 : count;
    for (int i = 0; i < show; i++) {
        time_t t = (time_t)entries[i].ts;
        char tbuf[NC_FMT_MAX];
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", localtime(&t));
        char line[NC_LABEL_MAX];
        snprintf(line, sizeof(line), "  %s  %s  %s\n", entries[i].id, tbuf,
                 entries[i].preview[0] ? entries[i].preview : "(empty)");
        strbuf_append_str(&sb, line);
    }
    if (count > 20) {
        char more[NC_FMT_MAX];
        snprintf(more, sizeof(more), "  ... and %d more\n", count - 20);
        strbuf_append_str(&sb, more);
    }
    if (count == 0)
        strbuf_append_str(&sb, "(no sessions found)");

    free(entries);
    *out = strbuf_detach(&sb);
    return count;
}

char *history_gen_session_id(void) {
    char id_buf[NC_FMT_MAX];
    unsigned char rnd[6];
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t n = fread(rnd, 1, sizeof(rnd), f);
        fclose(f);
        if (n == sizeof(rnd)) {
            snprintf(id_buf, sizeof(id_buf), "%02x%02x%02x%02x%02x%02x", rnd[0], rnd[1], rnd[2],
                     rnd[3], rnd[4], rnd[5]);
            return nc_xstrdup(id_buf);
        }
    }
    snprintf(id_buf, sizeof(id_buf), "%08x%04x", nc_rand_u32(), (uint16_t)(nc_rand_u32() & 0xFFFF));
    return nc_xstrdup(id_buf);
}

int history_rename(const char *session_id, const char *name) {
    char *dir = history_dir();
    if (!dir)
        return -1;

    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s.json", dir, session_id);
    free(dir);

    char *buf = strbuf_read_file(path, NULL);
    if (!buf)
        return -1;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
        return -1;

    cJSON *existing = cJSON_GetObjectItem(root, "name");
    if (existing)
        cJSON_ReplaceItemInObject(root, "name", cJSON_CreateString(name));
    else
        cJSON_AddStringToObject(root, "name", name);

    char *json = nc_cjson_detach_pretty(root);
    int rc = nc_write_file_str(path, json);
    free(json);
    return rc;
}

int history_search_fuzzy(const char *query, char **out) {
    char *dir = history_dir();
    if (!dir) {
        *out = nc_xstrdup("(no history directory)");
        return 0;
    }

    DIR *d = opendir(dir);
    if (!d) {
        free(dir);
        *out = nc_xstrdup("(no sessions found)");
        return 0;
    }

    typedef struct {
        char id[NC_FMT_MAX];
        double ts;
        char preview[NC_TAG_MAX];
        int score;
    } entry_t;
    entry_t *entries = NULL;
    int count = 0, cap = 0;

    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (!str_has_suffix(ent->d_name, ".json"))
            continue;

        char path[NC_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        char *buf = strbuf_read_file(path, NULL);
        if (!buf)
            continue;

        cJSON *root = cJSON_Parse(buf);
        free(buf);
        if (!root)
            continue;

        char searchable[NC_MSG_MAX] = "";
        const char *sname = cJSON_GetStringValue(cJSON_GetObjectItem(root, "name"));
        if (sname && sname[0])
            snprintf(searchable, sizeof(searchable), "%s ", sname);

        cJSON *msgs = cJSON_GetObjectItem(root, "messages");
        int nm = cJSON_GetArraySize(msgs);
        for (int i = 0; i < nm && i < 5; i++) {
            cJSON *m = cJSON_GetArrayItem(msgs, i);
            const char *role = cJSON_GetStringValue(cJSON_GetObjectItem(m, "role"));
            if (role && strcmp(role, "user") == 0) {
                cJSON *blocks = cJSON_GetObjectItem(m, "blocks");
                cJSON *b0 = cJSON_GetArrayItem(blocks, 0);
                if (b0) {
                    const char *txt = cJSON_GetStringValue(cJSON_GetObjectItem(b0, "text"));
                    if (txt) {
                        size_t slen = strlen(searchable);
                        snprintf(searchable + slen, sizeof(searchable) - slen, "%.200s ", txt);
                    }
                }
            }
        }

        int score = fuzzy_score(searchable, query);
        if (score < 0x7FFFFFFF) {
            if (count >= cap) {
                cap = cap ? cap * 2 : 16;
                entry_t *tmp = realloc(entries, cap * sizeof(entry_t));
                if (!tmp)
                    break;
                entries = tmp;
            }
            entry_t *e = &entries[count++];
            const char *sid = cJSON_GetStringValue(cJSON_GetObjectItem(root, "session_id"));
            nc_strlcpy(e->id, sid ? sid : "?", sizeof(e->id));
            cJSON *tsj = cJSON_GetObjectItem(root, "timestamp");
            e->ts = tsj ? tsj->valuedouble : 0;
            e->score = score;
            e->preview[0] = '\0';
            if (sname && sname[0])
                snprintf(e->preview, sizeof(e->preview), "[%s] ", sname);
            for (int i = 0; i < nm; i++) {
                cJSON *m = cJSON_GetArrayItem(msgs, i);
                const char *role = cJSON_GetStringValue(cJSON_GetObjectItem(m, "role"));
                if (role && strcmp(role, "user") == 0) {
                    cJSON *blocks = cJSON_GetObjectItem(m, "blocks");
                    cJSON *b0 = cJSON_GetArrayItem(blocks, 0);
                    if (b0) {
                        const char *txt = cJSON_GetStringValue(cJSON_GetObjectItem(b0, "text"));
                        if (txt) {
                            size_t plen = strlen(e->preview);
                            snprintf(e->preview + plen, sizeof(e->preview) - plen, "%.80s", txt);
                        }
                    }
                    break;
                }
            }
        }
        cJSON_Delete(root);
    }
    closedir(d);
    free(dir);

    /* Sort by fuzzy score ascending (best first) */
    for (int i = 0; i < count - 1; i++)
        for (int j = i + 1; j < count; j++)
            if (entries[j].score < entries[i].score) {
                entry_t tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }

    strbuf_t sb;
    strbuf_init(&sb);
    int show = count > 15 ? 15 : count;
    for (int i = 0; i < show; i++) {
        time_t t = (time_t)entries[i].ts;
        char tbuf[NC_FMT_MAX];
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", localtime(&t));
        char line[NC_LABEL_MAX];
        snprintf(line, sizeof(line), "  %s  %s  %s\n", entries[i].id, tbuf,
                 entries[i].preview[0] ? entries[i].preview : "(empty)");
        strbuf_append_str(&sb, line);
    }
    if (count > 15) {
        char more[NC_FMT_MAX];
        snprintf(more, sizeof(more), "  ... and %d more matches\n", count - 15);
        strbuf_append_str(&sb, more);
    }
    if (count == 0)
        strbuf_append_str(&sb, "(no matching sessions found)");

    free(entries);
    *out = strbuf_detach(&sb);
    return count;
}

int history_attach_file_snapshot(const char *session_id, const char *snapshot_ref) {
    if (!session_id || !snapshot_ref || !snapshot_ref[0])
        return -1;
    char path[NC_PATH_MAX];
    cJSON *root = history_load_root_file(session_id, path, sizeof(path));
    if (!root)
        return -1;

    cJSON *existing = cJSON_GetObjectItem(root, "file_snapshot_ref");
    if (existing)
        cJSON_ReplaceItemInObject(root, "file_snapshot_ref", cJSON_CreateString(snapshot_ref));
    else
        cJSON_AddStringToObject(root, "file_snapshot_ref", snapshot_ref);

    int rc = history_write_root_file(path, root);
    cJSON_Delete(root);
    return rc;
}

history_file_snapshot_status_t history_restore_file_snapshot(const char *session_id) {
    if (!session_id || !session_id[0])
        return HISTORY_FILE_SNAPSHOT_NONE;

    char path[NC_PATH_MAX];
    cJSON *root = history_load_root_file(session_id, path, sizeof(path));
    if (!root)
        return HISTORY_FILE_SNAPSHOT_ERROR;

    const char *snapshot_ref = cJSON_GetStringValue(cJSON_GetObjectItem(root, "file_snapshot_ref"));
    if (!snapshot_ref || !snapshot_ref[0]) {
        cJSON_Delete(root);
        return HISTORY_FILE_SNAPSHOT_NONE;
    }

    char cwd[NC_PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd)) || !git_is_repo(cwd)) {
        cJSON_Delete(root);
        return HISTORY_FILE_SNAPSHOT_UNAVAILABLE;
    }
    if (git_has_uncommitted(cwd)) {
        cJSON_Delete(root);
        return HISTORY_FILE_SNAPSHOT_SKIPPED_DIRTY;
    }
    if (git_stash_restore_exact(cwd, snapshot_ref) != 0) {
        cJSON_Delete(root);
        return HISTORY_FILE_SNAPSHOT_ERROR;
    }

    cJSON_DeleteItemFromObject(root, "file_snapshot_ref");
    int rc = history_write_root_file(path, root);
    cJSON_Delete(root);
    return rc == 0 ? HISTORY_FILE_SNAPSHOT_RESTORED : HISTORY_FILE_SNAPSHOT_ERROR;
}

char *history_file_snapshot_status_message(const char *session_id,
                                           history_file_snapshot_status_t status) {
    const char *sid = session_id && session_id[0] ? session_id : "session";
    char buf[NC_MSG_MAX];
    switch (status) {
    case HISTORY_FILE_SNAPSHOT_RESTORED:
        snprintf(buf, sizeof(buf), "(restored file snapshot for %s)\n", sid);
        return nc_xstrdup(buf);
    case HISTORY_FILE_SNAPSHOT_SKIPPED_DIRTY:
        snprintf(
            buf, sizeof(buf),
            "(file snapshot for %s left pending: current working tree has uncommitted changes)\n",
            sid);
        return nc_xstrdup(buf);
    case HISTORY_FILE_SNAPSHOT_UNAVAILABLE:
        snprintf(buf, sizeof(buf),
                 "(file snapshot for %s left pending: resume from the matching git working tree)\n",
                 sid);
        return nc_xstrdup(buf);
    case HISTORY_FILE_SNAPSHOT_ERROR:
        snprintf(buf, sizeof(buf), "(file snapshot restore failed for %s)\n", sid);
        return nc_xstrdup(buf);
    case HISTORY_FILE_SNAPSHOT_NONE:
    default:
        return NULL;
    }
}

char *history_find_latest(void) {
    char *dir = history_dir();
    if (!dir)
        return NULL;

    DIR *d = opendir(dir);
    if (!d) {
        free(dir);
        return NULL;
    }

    double best_ts = 0;
    char best_id[NC_FMT_MAX] = "";

    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (!str_has_suffix(ent->d_name, ".json"))
            continue;

        char path[NC_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        struct stat st;
        if (stat(path, &st) != 0)
            continue;
        double mtime = (double)st.st_mtime;
        if (mtime > best_ts) {
            best_ts = mtime;
            size_t nlen = strlen(ent->d_name);
            char sid[NC_FMT_MAX];
            snprintf(sid, sizeof(sid), "%.*s", (int)(nlen - 5), ent->d_name);
            nc_strlcpy(best_id, sid, sizeof(best_id));
        }
    }
    closedir(d);
    free(dir);

    return best_id[0] ? nc_xstrdup(best_id) : NULL;
}
