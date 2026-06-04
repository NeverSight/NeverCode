/*
 * shared_pad.c — Concurrent shared findings board for multi-agent runs.
 *
 * Multiple agents write discoveries; duplicates are filtered on merge.
 * Backed by a mutex for thread safety and optional file persistence.
 */

#include "harness.h"

static void _shared_pad_set_run_token_locked(shared_pad_t *sp, const char *run_token) {
    if (!sp)
        return;
    sp->current_run_token[0] = '\0';
    if (run_token && run_token[0])
        nc_strlcpy(sp->current_run_token, run_token, sizeof(sp->current_run_token));
}

static bool _shared_pad_parse_meta_line_locked(shared_pad_t *sp, const char *line) {
    static const char prefix[] = "<!-- " SPAD_META_MAGIC " ";
    if (!sp || !line || !nc_starts_with(line, prefix))
        return false;

    const char *json = line + sizeof(prefix) - 1;
    const char *suffix = strstr(json, "-->");
    if (!suffix)
        return false;
    while (suffix > json && (suffix[-1] == ' ' || suffix[-1] == '\t'))
        suffix--;
    if (suffix <= json)
        return false;

    size_t json_len = (size_t)(suffix - json);
    char *meta = malloc(json_len + 1);
    if (!meta)
        return false;
    memcpy(meta, json, json_len);
    meta[json_len] = '\0';

    cJSON *obj = cJSON_Parse(meta);
    free(meta);
    if (!cJSON_IsObject(obj)) {
        cJSON_Delete(obj);
        return false;
    }

    cJSON *run_token = cJSON_GetObjectItem(obj, "run_token");
    if (cJSON_IsString(run_token) && run_token->valuestring)
        _shared_pad_set_run_token_locked(sp, run_token->valuestring);
    cJSON_Delete(obj);
    return true;
}

static void _shared_pad_clear_locked(shared_pad_t *sp) {
    for (int i = 0; i < sp->nlines; i++) {
        free(sp->lines[i].text);
        memset(&sp->lines[i], 0, sizeof(sp->lines[i]));
    }
    sp->nlines = 0;
}

static int _shared_pad_add_locked(shared_pad_t *sp, const char *author, const char *text,
                                  bool dedup, long timestamp) {
    if (!sp || !text || !text[0])
        return 0;

    if (dedup) {
        for (int i = 0; i < sp->nlines; i++) {
            if (sp->lines[i].text && strcmp(sp->lines[i].text, text) == 0)
                return 0;
        }
    }

    if (sp->nlines >= SPAD_MAX_LINES)
        return -1;

    spad_line_t *ln = &sp->lines[sp->nlines];
    memset(ln, 0, sizeof(*ln));
    ln->text = nc_xstrdup(text);
    if (!ln->text)
        return -1;
    if (author)
        nc_strlcpy(ln->author, author, sizeof(ln->author));
    ln->timestamp = timestamp > 0 ? timestamp : nc_wall_sec();
    sp->nlines++;
    return 1;
}

static int _shared_pad_apply_snapshot_locked(shared_pad_t *sp, const shared_pad_snapshot_t *snap,
                                             bool dedup, int *out_added) {
    if (!sp || !snap)
        return -1;

    int added = 0;
    long now = nc_wall_sec();
    for (int i = 0; i < snap->count && i < SPAD_MAX_LINES; i++) {
        if (!snap->lines[i].text[0])
            continue;
        long timestamp = now - snap->lines[i].age_s;
        if (timestamp < 0)
            timestamp = 0;
        int rc = _shared_pad_add_locked(sp, snap->lines[i].author[0] ? snap->lines[i].author : NULL,
                                        snap->lines[i].text, dedup, timestamp);
        if (rc < 0)
            return -1;
        if (rc > 0)
            added++;
    }
    if (out_added)
        *out_added = added;
    return 0;
}

void shared_pad_init(shared_pad_t *sp) {
    memset(sp, 0, sizeof(*sp));
    pthread_mutex_init(&sp->lock, NULL);
}

void shared_pad_init_persistent(shared_pad_t *sp, const char *path) {
    shared_pad_init(sp);
    if (path)
        nc_strlcpy(sp->path, path, sizeof(sp->path));
    sp->persistent = true;
}

void shared_pad_free(shared_pad_t *sp) {
    for (int i = 0; i < sp->nlines; i++)
        free(sp->lines[i].text);
    pthread_mutex_destroy(&sp->lock);
    memset(sp, 0, sizeof(*sp));
}

void shared_pad_set_run_token(shared_pad_t *sp, const char *run_token) {
    if (!sp)
        return;
    pthread_mutex_lock(&sp->lock);
    _shared_pad_set_run_token_locked(sp, run_token);
    pthread_mutex_unlock(&sp->lock);
}

int shared_pad_append(shared_pad_t *sp, const char *author, const char *text) {
    if (!text || !text[0])
        return -1;

    pthread_mutex_lock(&sp->lock);
    int rc = _shared_pad_add_locked(sp, author, text, false, nc_wall_sec());
    pthread_mutex_unlock(&sp->lock);

    if (rc > 0 && sp->persistent && sp->path[0] && shared_pad_persist(sp) != 0)
        return -1;
    return rc > 0 ? 0 : rc;
}

int shared_pad_merge(shared_pad_t *sp, const char *author, const char *text) {
    if (!text || !text[0])
        return -1;

    int added = 0;
    bool failed = false;
    char *dup = nc_xstrdup(text);
    if (!dup)
        return -1;

    pthread_mutex_lock(&sp->lock);
    char *line = strtok(dup, "\n");
    while (line) {
        while (*line == ' ' || *line == '\t')
            line++;
        if (*line) {
            int rc = _shared_pad_add_locked(sp, author, line, true, nc_wall_sec());
            if (rc < 0) {
                failed = true;
                break;
            }
            if (rc > 0)
                added++;
        }
        line = strtok(NULL, "\n");
    }
    pthread_mutex_unlock(&sp->lock);
    free(dup);

    if (failed)
        return -1;
    if (added > 0 && sp->persistent && sp->path[0] && shared_pad_persist(sp) != 0)
        return -1;
    return added;
}

int shared_pad_line_count(shared_pad_t *sp) {
    pthread_mutex_lock(&sp->lock);
    int n = sp->nlines;
    pthread_mutex_unlock(&sp->lock);
    return n;
}

char *shared_pad_read(shared_pad_t *sp) {
    strbuf_t sb;
    strbuf_init(&sb);

    pthread_mutex_lock(&sp->lock);
    for (int i = 0; i < sp->nlines; i++) {
        if (sp->lines[i].author[0])
            strbuf_appendf(&sb, "[%s] ", sp->lines[i].author);
        strbuf_append_str(&sb, sp->lines[i].text);
        strbuf_append_str(&sb, "\n");
    }
    pthread_mutex_unlock(&sp->lock);

    return strbuf_detach(&sb);
}

int shared_pad_persist(shared_pad_t *sp) {
    if (!sp || !sp->path[0])
        return -1;

    strbuf_t sb;
    strbuf_init(&sb);

    pthread_mutex_lock(&sp->lock);
    if (sp->current_run_token[0]) {
        cJSON *obj = cJSON_CreateObject();
        if (obj) {
            cJSON_AddStringToObject(obj, "run_token", sp->current_run_token);
            char *printed = nc_cjson_detach(obj);
            if (printed) {
                strbuf_appendf(&sb, "<!-- %s %s -->\n", SPAD_META_MAGIC, printed);
                free(printed);
            }
        }
    }
    for (int i = 0; i < sp->nlines; i++) {
        if (sp->lines[i].author[0])
            strbuf_appendf(&sb, "## %s\n", sp->lines[i].author);
        strbuf_appendf(&sb, "%s\n", sp->lines[i].text);
    }
    pthread_mutex_unlock(&sp->lock);

    size_t body_len = sb.len;
    char *body = strbuf_detach(&sb);
    int rc = nc_write_file_atomic(sp->path, body, body_len);
    free(body);
    return rc;
}

int shared_pad_load(shared_pad_t *sp) {
    if (!sp->path[0])
        return -1;

    char *buf = strbuf_read_file_text(sp->path, NULL);
    if (!buf)
        return -1;

    bool failed = false;
    pthread_mutex_lock(&sp->lock);
    _shared_pad_clear_locked(sp);
    _shared_pad_set_run_token_locked(sp, NULL);

    char author[SPAD_AUTHOR_MAX] = {0};
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';

        if (_shared_pad_parse_meta_line_locked(sp, line)) {
            /* metadata only */
        } else if (line[0] == '#' && line[1] == '#' && line[2] == ' ') {
            nc_strlcpy(author, line + 3, sizeof(author));
        } else if (line[0]) {
            if (_shared_pad_add_locked(sp, author[0] ? author : NULL, line, false, nc_wall_sec()) <
                0) {
                failed = true;
                break;
            }
        }
        line = nl ? nl + 1 : NULL;
    }

    pthread_mutex_unlock(&sp->lock);
    free(buf);
    return failed ? -1 : 0;
}

char *shared_pad_build_suffix(const shared_pad_t *sp) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(
        &sb, "\n---\nSHARED PAD: Read for context from other agents. "
             "Append critical findings concisely. Only add what is not already there.\n---\n");
    return strbuf_detach(&sb);
}

void shared_pad_snapshot(const shared_pad_t *sp, shared_pad_snapshot_t *out) {
    if (!sp || !out)
        return;

    memset(out, 0, sizeof(*out));
    pthread_mutex_lock((pthread_mutex_t *)&sp->lock);
    out->count = sp->nlines;
    out->persistent = sp->persistent;
    nc_strlcpy(out->path, sp->path, sizeof(out->path));
    nc_strlcpy(out->run_token, sp->current_run_token, sizeof(out->run_token));

    long now = nc_wall_sec();
    for (int i = 0; i < sp->nlines && i < SPAD_MAX_LINES; i++) {
        if (sp->lines[i].text)
            nc_strlcpy(out->lines[i].text, sp->lines[i].text, sizeof(out->lines[i].text));
        nc_strlcpy(out->lines[i].author, sp->lines[i].author, sizeof(out->lines[i].author));
        out->lines[i].age_s = sp->lines[i].timestamp > 0 ? now - sp->lines[i].timestamp : 0;
        if (out->lines[i].age_s < 0)
            out->lines[i].age_s = 0;
    }
    pthread_mutex_unlock((pthread_mutex_t *)&sp->lock);
}

int shared_pad_merge_snapshot(shared_pad_t *sp, const shared_pad_snapshot_t *snap) {
    if (!sp || !snap)
        return -1;

    pthread_mutex_lock(&sp->lock);
    if (sp->current_run_token[0] && snap->run_token[0] &&
        strcmp(sp->current_run_token, snap->run_token) != 0) {
        pthread_mutex_unlock(&sp->lock);
        return 0;
    }
    if (snap->path[0] && !sp->path[0])
        nc_strlcpy(sp->path, snap->path, sizeof(sp->path));
    if (!sp->current_run_token[0] && snap->run_token[0])
        nc_strlcpy(sp->current_run_token, snap->run_token, sizeof(sp->current_run_token));
    sp->persistent = sp->persistent || snap->persistent;

    int added = 0;
    int rc = _shared_pad_apply_snapshot_locked(sp, snap, true, &added);
    pthread_mutex_unlock(&sp->lock);

    if (rc != 0)
        return -1;
    if (added > 0 && sp->persistent && sp->path[0] && shared_pad_persist(sp) != 0)
        return -1;
    return added;
}

void shared_pad_restore(shared_pad_t *sp, const shared_pad_snapshot_t *snap) {
    if (!sp)
        return;

    pthread_mutex_lock(&sp->lock);
    _shared_pad_clear_locked(sp);
    if (snap) {
        if (snap->path[0])
            nc_strlcpy(sp->path, snap->path, sizeof(sp->path));
        if (snap->run_token[0])
            nc_strlcpy(sp->current_run_token, snap->run_token, sizeof(sp->current_run_token));
        sp->persistent = sp->persistent || snap->persistent;
        (void)_shared_pad_apply_snapshot_locked(sp, snap, false, NULL);
    }
    pthread_mutex_unlock(&sp->lock);
}
