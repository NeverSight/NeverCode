/*
 * event_ledger.c — Append-only event log with snapshot dual-write.
 *
 * Every state transition is appended to a JSONL event log for audit
 * and replay.  A separate snapshot file holds the current materialized
 * state for fast load on resume.
 */

#include "harness.h"

void ledger_init(event_ledger_t *el) {
    memset(el, 0, sizeof(*el));
    el->next_seq = 1;
    el->nwatchers = 0;
}

void ledger_init_persistent(event_ledger_t *el, const char *log_path, const char *snap_path) {
    ledger_init(el);
    if (log_path)
        nc_strlcpy(el->log_path, log_path, sizeof(el->log_path));
    if (snap_path)
        nc_strlcpy(el->snap_path, snap_path, sizeof(el->snap_path));
    el->persistent = true;
}

void ledger_free(event_ledger_t *el) {
    memset(el, 0, sizeof(*el));
}

void ledger_set_run_token(event_ledger_t *el, const char *run_token) {
    if (!el)
        return;
    el->current_run_token[0] = '\0';
    if (run_token && run_token[0])
        nc_strlcpy(el->current_run_token, run_token, sizeof(el->current_run_token));
}

void ledger_prepare_new_run(event_ledger_t *el) {
    if (!el || !el->persistent)
        return;
    if (el->next_seq != 1 || el->nevents != 0)
        return;
    if (el->snap_path[0])
        (void)unlink(el->snap_path);
}

static char *_ledger_event_json_line(const ledger_event_t *ev) {
    if (!ev)
        return NULL;
    cJSON *obj = cJSON_CreateObject();
    if (!obj)
        return NULL;

    cJSON_AddNumberToObject(obj, "seq", ev->sequence);
    cJSON_AddNumberToObject(obj, "ts", ev->timestamp);
    cJSON_AddStringToObject(obj, "type", ev->type);
    if (ev->node_id[0])
        cJSON_AddStringToObject(obj, "node", ev->node_id);
    if (ev->detail[0])
        cJSON_AddStringToObject(obj, "detail", ev->detail);
    if (ev->has_output)
        cJSON_AddStringToObject(obj, "output", ev->output);
    if (ev->has_exit_code)
        cJSON_AddNumberToObject(obj, "exit_code", ev->exit_code);
    if (ev->has_run_token)
        cJSON_AddStringToObject(obj, "run_token", ev->run_token);

    char *line = nc_cjson_detach(obj);
    return line;
}

static int _ledger_write_event_json(FILE *f, const ledger_event_t *ev) {
    if (!f || !ev)
        return -1;

    char *line = _ledger_event_json_line(ev);
    if (!line)
        return -1;

    int rc = fputs(line, f);
    if (rc >= 0)
        rc = fputc('\n', f);
    free(line);
    return rc >= 0 ? 0 : -1;
}

static int _ledger_append_event_json(strbuf_t *sb, const ledger_event_t *ev) {
    if (!sb || !ev)
        return -1;

    char *line = _ledger_event_json_line(ev);
    if (!line)
        return -1;
    strbuf_append_str(sb, line);
    strbuf_append_str(sb, "\n");
    free(line);
    return 0;
}

static int _ledger_append_common(event_ledger_t *el, const char *type, const char *node_id,
                                 const char *detail, const char *output, int exit_code,
                                 bool has_exit_code) {
    if (el->nevents >= LEDGER_MAX_EVENTS)
        return -1;

    ledger_event_t *ev = &el->events[el->nevents++];
    memset(ev, 0, sizeof(*ev));
    if (type)
        nc_strlcpy(ev->type, type, sizeof(ev->type));
    if (node_id)
        nc_strlcpy(ev->node_id, node_id, sizeof(ev->node_id));
    if (detail)
        nc_strlcpy(ev->detail, detail, sizeof(ev->detail));
    if (output) {
        ev->has_output = true;
        nc_strlcpy(ev->output, output, sizeof(ev->output));
    }
    if (has_exit_code) {
        ev->has_exit_code = true;
        ev->exit_code = exit_code;
    }
    if (el->current_run_token[0]) {
        ev->has_run_token = true;
        nc_strlcpy(ev->run_token, el->current_run_token, sizeof(ev->run_token));
    }
    ev->timestamp = nc_wall_sec();
    ev->sequence = el->next_seq++;

    if (el->persistent && el->log_path[0]) {
        const char *mode = (ev->sequence == 1 && strcmp(ev->type, "run_started") == 0) ? "w" : "a";
        FILE *f = fopen(el->log_path, mode);
        if (f) {
            (void)_ledger_write_event_json(f, ev);
            (void)fflush(f);
            fclose(f);
        }
    }

    ledger_notify_watchers(el, ev);

    return ev->sequence;
}

int ledger_append(event_ledger_t *el, const char *type, const char *node_id, const char *detail) {
    return _ledger_append_common(el, type, node_id, detail, NULL, 0, false);
}

int ledger_append_result(event_ledger_t *el, const char *type, const char *node_id,
                         const char *detail, const char *output, int exit_code) {
    return _ledger_append_common(el, type, node_id, detail, output, exit_code, true);
}

int ledger_flush_log(event_ledger_t *el) {
    if (!el->persistent || !el->log_path[0])
        return -1;

    strbuf_t sb;
    strbuf_init(&sb);

    for (int i = 0; i < el->nevents; i++) {
        const ledger_event_t *ev = &el->events[i];
        if (_ledger_append_event_json(&sb, ev) != 0) {
            strbuf_free(&sb);
            return -1;
        }
    }

    size_t body_len = sb.len;
    char *body = strbuf_detach(&sb);
    int rc = nc_write_file_atomic(el->log_path, body, body_len);
    free(body);
    return rc;
}

int ledger_write_snapshot(event_ledger_t *el, const char *json_state) {
    if (!el->persistent || !el->snap_path[0] || !json_state)
        return -1;

    return nc_write_file_atomic(el->snap_path, json_state, strlen(json_state));
}

int ledger_load_log(event_ledger_t *el) {
    if (!el->log_path[0])
        return -1;

    FILE *f = fopen(el->log_path, "r");
    if (!f)
        return 0;

    memset(el->events, 0, sizeof(el->events));
    el->nevents = 0;
    el->next_seq = 1;

    char *line = NULL;
    size_t line_cap = 0;
    int loaded = 0;
    ssize_t line_len = 0;
    while ((line_len = getline(&line, &line_cap, f)) >= 0 && el->nevents < LEDGER_MAX_EVENTS) {
        if (line_len == 0)
            continue;
        cJSON *obj = cJSON_Parse(line);
        if (!obj || !cJSON_IsObject(obj)) {
            cJSON_Delete(obj);
            continue;
        }

        cJSON *seq = cJSON_GetObjectItem(obj, "seq");
        if (!cJSON_IsNumber(seq) || seq->valueint <= 0) {
            cJSON_Delete(obj);
            continue;
        }

        ledger_event_t *ev = &el->events[el->nevents];
        memset(ev, 0, sizeof(*ev));
        ev->sequence = seq->valueint;

        cJSON *ts = cJSON_GetObjectItem(obj, "ts");
        if (cJSON_IsNumber(ts))
            ev->timestamp = (long)cJSON_GetNumberValue(ts);

        cJSON *type = cJSON_GetObjectItem(obj, "type");
        if (cJSON_IsString(type) && type->valuestring)
            nc_strlcpy(ev->type, type->valuestring, sizeof(ev->type));

        cJSON *node = cJSON_GetObjectItem(obj, "node");
        if (cJSON_IsString(node) && node->valuestring)
            nc_strlcpy(ev->node_id, node->valuestring, sizeof(ev->node_id));

        cJSON *detail = cJSON_GetObjectItem(obj, "detail");
        if (cJSON_IsString(detail) && detail->valuestring)
            nc_strlcpy(ev->detail, detail->valuestring, sizeof(ev->detail));

        cJSON *output = cJSON_GetObjectItem(obj, "output");
        if (cJSON_IsString(output) && output->valuestring) {
            ev->has_output = true;
            nc_strlcpy(ev->output, output->valuestring, sizeof(ev->output));
        }

        cJSON *exit_code = cJSON_GetObjectItem(obj, "exit_code");
        if (cJSON_IsNumber(exit_code)) {
            ev->has_exit_code = true;
            ev->exit_code = exit_code->valueint;
        }

        cJSON *run_token = cJSON_GetObjectItem(obj, "run_token");
        if (cJSON_IsString(run_token) && run_token->valuestring) {
            ev->has_run_token = true;
            nc_strlcpy(ev->run_token, run_token->valuestring, sizeof(ev->run_token));
            if (!el->current_run_token[0])
                nc_strlcpy(el->current_run_token, ev->run_token, sizeof(el->current_run_token));
        }

        el->nevents++;
        if (ev->sequence >= el->next_seq)
            el->next_seq = ev->sequence + 1;
        loaded++;
        cJSON_Delete(obj);
    }
    fclose(f);
    free(line);
    return loaded;
}

int ledger_event_count(const event_ledger_t *el) {
    return el->nevents;
}

const ledger_event_t *ledger_event_at(const event_ledger_t *el, int idx) {
    if (idx < 0 || idx >= el->nevents)
        return NULL;
    return &el->events[idx];
}

int ledger_events_since(const event_ledger_t *el, int seq, const ledger_event_t **out, int max) {
    int n = 0;
    for (int i = 0; i < el->nevents && n < max; i++)
        if (el->events[i].sequence > seq)
            out[n++] = &el->events[i];
    return n;
}

char *ledger_format_tail(const event_ledger_t *el, int n) {
    strbuf_t sb;
    strbuf_init(&sb);

    int start = el->nevents - n;
    if (start < 0)
        start = 0;

    strbuf_appendf(&sb, "Event Ledger: %d events (showing last %d)\n", el->nevents,
                   el->nevents - start);

    for (int i = start; i < el->nevents; i++) {
        const ledger_event_t *ev = &el->events[i];
        strbuf_appendf(&sb, "  #%d [%s]", ev->sequence, ev->type);
        if (ev->node_id[0])
            strbuf_appendf(&sb, " node=%s", ev->node_id);
        if (ev->detail[0])
            strbuf_appendf(&sb, " %.*s", 80, ev->detail);
        strbuf_append_str(&sb, "\n");
    }
    return strbuf_detach(&sb);
}
