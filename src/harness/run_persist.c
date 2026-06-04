/*
 * run_persist.c — Full orchestrator state persistence and restore.
 *
 * Serializes the pipeline graph together with the orchestrator's
 * durable scheduling state. On reload, it reconstructs node status,
 * retry-cycle counters, success criteria, probe state, and queued
 * control actions so an interrupted run can continue with the same
 * supervision and completion rules.
 */

#include "harness.h"
#include "cJSON.h"

static const char *_find_key(const char *json, const char *json_end, const char *key);

static const char *_skip_ws(const char *p, const char *limit) {
    if (!p)
        return NULL;
    while (p < limit && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;
    return p;
}

static const char *_scan_balanced(const char *start, const char *limit, char open_ch,
                                  char close_ch) {
    if (!start || !limit || start >= limit || *start != open_ch)
        return NULL;
    int depth = 1;
    const char *p = start + 1;
    bool in_string = false;
    bool escaped = false;
    while (p < limit && depth > 0) {
        char ch = *p;
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            p++;
            continue;
        }
        if (ch == '"') {
            in_string = true;
        } else if (ch == open_ch) {
            depth++;
        } else if (ch == close_ch) {
            depth--;
        }
        p++;
    }
    return depth == 0 ? p : NULL;
}

static const char *_find_compound_in(const char *json, const char *json_end, const char *key,
                                     char open_ch, char close_ch, const char **out_end) {
    const char *limit = json_end ? json_end : json + strlen(json);
    const char *start = _find_key(json, json_end, key);
    if (!start)
        return NULL;
    start = _skip_ws(start, limit);
    if (!start || start >= limit || *start != open_ch)
        return NULL;
    const char *end = _scan_balanced(start, limit, open_ch, close_ch);
    if (!end)
        return NULL;
    if (out_end)
        *out_end = end;
    return start;
}

static const char *_find_array_in(const char *json, const char *json_end, const char *key,
                                  const char **out_end) {
    return _find_compound_in(json, json_end, key, '[', ']', out_end);
}

static const char *_find_array(const char *json, const char *key, const char **out_end) {
    return _find_array_in(json, NULL, key, out_end);
}

static int _extract_bool_in(const char *json, const char *json_end, const char *key) {
    const char *val = _find_key(json, json_end, key);
    if (!val)
        return 0;
    return nc_starts_with(val, "true");
}

static unsigned long _extract_ulong_in(const char *json, const char *json_end, const char *key) {
    const char *val = _find_key(json, json_end, key);
    if (!val)
        return 0;
    return strtoul(val, NULL, 10);
}

static double _extract_double_in(const char *json, const char *json_end, const char *key) {
    const char *val = _find_key(json, json_end, key);
    if (!val)
        return 0.0;
    return nc_safe_atof(val, 0.0);
}

static int _parse_sc_kind(const char *s) {
    if (!s)
        return -1;
    if (strcmp(s, "output_contains") == 0)
        return SC_OUTPUT_CONTAINS;
    if (strcmp(s, "file_exists") == 0)
        return SC_FILE_EXISTS;
    if (strcmp(s, "file_contains") == 0)
        return SC_FILE_CONTAINS;
    if (strcmp(s, "file_nonempty") == 0)
        return SC_FILE_NONEMPTY;
    if (strcmp(s, "exit_zero") == 0)
        return SC_EXIT_CODE_ZERO;
    if (strcmp(s, "output_not_contains") == 0)
        return SC_OUTPUT_NOT_CONTAINS;
    return -1;
}

static void _append_json_string(strbuf_t *sb, const char *value) {
    cJSON *item = cJSON_CreateString(value ? value : "");
    char *printed = item ? cJSON_PrintUnformatted(item) : NULL;
    if (printed)
        strbuf_append_str(sb, printed);
    else
        strbuf_append_str(sb, "\"\"");
    cJSON_Delete(item);
    free(printed);
}

static int _json_hex_value(char ch) {
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

static size_t _append_utf8_codepoint(char *dst, unsigned codepoint) {
    if (!dst)
        return 0;
    if (codepoint <= 0x7F) {
        dst[0] = (char)codepoint;
        return 1;
    }
    if (codepoint <= 0x7FF) {
        dst[0] = (char)(0xC0 | ((codepoint >> 6) & 0x1F));
        dst[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF)
        codepoint = '?';
    if (codepoint <= 0xFFFF) {
        dst[0] = (char)(0xE0 | ((codepoint >> 12) & 0x0F));
        dst[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    if (codepoint <= 0x10FFFF) {
        dst[0] = (char)(0xF0 | ((codepoint >> 18) & 0x07));
        dst[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        dst[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        dst[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    dst[0] = '?';
    return 1;
}

static char *_dup_json_string_token(const char **cursor, const char *limit) {
    if (!cursor || !*cursor || !limit || *cursor >= limit || **cursor != '"')
        return NULL;

    const char *p = *cursor + 1;
    size_t cap = (size_t)(limit - p) + 1;
    char *buf = malloc(cap);
    if (!buf)
        return NULL;

    size_t out_len = 0;
    while (p < limit) {
        char ch = *p++;
        if (ch == '"') {
            buf[out_len] = '\0';
            *cursor = p;
            return buf;
        }
        if (ch != '\\') {
            buf[out_len++] = ch;
            continue;
        }
        if (p >= limit)
            break;
        ch = *p++;
        switch (ch) {
        case '"':
        case '\\':
        case '/':
            buf[out_len++] = ch;
            break;
        case 'b':
            buf[out_len++] = '\b';
            break;
        case 'f':
            buf[out_len++] = '\f';
            break;
        case 'n':
            buf[out_len++] = '\n';
            break;
        case 'r':
            buf[out_len++] = '\r';
            break;
        case 't':
            buf[out_len++] = '\t';
            break;
        case 'u': {
            if (limit - p < 4)
                goto fail;
            int h0 = _json_hex_value(p[0]);
            int h1 = _json_hex_value(p[1]);
            int h2 = _json_hex_value(p[2]);
            int h3 = _json_hex_value(p[3]);
            if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0)
                goto fail;
            unsigned codepoint =
                ((unsigned)h0 << 12) | ((unsigned)h1 << 8) | ((unsigned)h2 << 4) | (unsigned)h3;
            p += 4;
            out_len += _append_utf8_codepoint(buf + out_len, codepoint);
            break;
        }
        default:
            buf[out_len++] = ch;
            break;
        }
    }

fail:
    free(buf);
    return NULL;
}

char *run_persist_snapshot_json(const pipe_orch_t *o) {
    if (!o || !o->pipeline)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    const pipeline_t *p = o->pipeline;
    watchdog_snapshot_t wd;
    failure_router_snapshot_t fr;
    stall_snapshot_t stall;
    context_fatigue_snapshot_t fatigue;
    output_eval_snapshot_t oeval;
    auto_test_snapshot_t atest;
    checkpoint_discipline_snapshot_t checkpoint;
    tool_verify_snapshot_t tv;
    doom_loop_snapshot_t doom;
    shared_pad_snapshot_t pad;
    watchdog_snapshot(&wd);
    failure_router_snapshot(&fr);
    stall_detector_snapshot(&stall);
    context_fatigue_snapshot(&fatigue);
    output_eval_snapshot(&oeval);
    auto_test_snapshot(&atest);
    checkpoint_discipline_snapshot(&checkpoint);
    tool_verify_snapshot(&tv);
    doom_loop_snapshot(&doom);
    shared_pad_snapshot(&o->pad, &pad);

    strbuf_appendf(&sb, "{\n");
    strbuf_appendf(&sb, "  \"magic\": ");
    _append_json_string(&sb, RUN_PERSIST_MAGIC);
    strbuf_appendf(&sb, ",\n");
    strbuf_appendf(&sb, "  \"version\": %d,\n", RUN_PERSIST_VERSION);
    strbuf_appendf(&sb, "  \"timestamp\": %ld,\n", nc_wall_sec());

    strbuf_appendf(&sb, "  \"pipeline\": {\n");
    strbuf_appendf(&sb, "    \"name\": ");
    _append_json_string(&sb, p->name);
    strbuf_appendf(&sb, ",\n");
    strbuf_appendf(&sb, "    \"concurrency\": %d,\n", p->concurrency);
    strbuf_appendf(&sb, "    \"max_iterations\": %d,\n", p->max_iterations);
    strbuf_appendf(&sb, "    \"fail_fast\": %s,\n", p->fail_fast ? "true" : "false");
    strbuf_appendf(&sb, "    \"status\": ");
    _append_json_string(&sb, pipe_status_name(p->status));
    strbuf_appendf(&sb, ",\n");
    strbuf_appendf(&sb, "    \"started_ms\": %ld,\n", p->started_ms);
    strbuf_appendf(&sb, "    \"finished_ms\": %ld\n", p->finished_ms);
    strbuf_appendf(&sb, "  },\n");

    strbuf_appendf(&sb, "  \"orchestrator\": {\n");
    strbuf_appendf(&sb, "    \"state\": %d,\n", o->state);
    strbuf_appendf(&sb, "    \"started_ms\": %ld,\n", o->started_ms);
    strbuf_appendf(&sb, "    \"finished_ms\": %ld,\n", o->finished_ms);
    strbuf_appendf(&sb, "    \"ctx_rendering\": %s,\n", o->ctx_rendering ? "true" : "false");
    strbuf_appendf(&sb, "    \"run_scope_state\": %d,\n", (int)o->run_scope.state);
    strbuf_appendf(&sb, "    \"run_scope_token\": ");
    _append_json_string(&sb, o->run_scope.token);
    strbuf_appendf(&sb, ",\n");
    strbuf_appendf(&sb, "    \"run_scope_requested_age_ms\": %ld,\n",
                   o->run_scope.requested_ms > 0 ? nc_mono_ms() - o->run_scope.requested_ms : 0L);
    strbuf_appendf(&sb, "    \"ncriteria\": %d,\n", o->ncriteria);
    strbuf_appendf(&sb, "    \"nprobes\": %d\n", o->nprobes);
    strbuf_appendf(&sb, "  },\n");

    strbuf_appendf(&sb, "  \"nodes\": [\n");
    for (int i = 0; i < p->nnodes; i++) {
        const pipe_node_t *n = &p->nodes[i];
        strbuf_appendf(&sb, "    {\n");
        strbuf_appendf(&sb, "      \"id\": ");
        _append_json_string(&sb, n->id);
        strbuf_appendf(&sb, ",\n");
        strbuf_appendf(&sb, "      \"status\": ");
        _append_json_string(&sb, pipe_status_name(n->status));
        strbuf_appendf(&sb, ",\n");
        strbuf_appendf(&sb, "      \"exit_code\": %d,\n", n->exit_code);
        strbuf_appendf(&sb, "      \"attempt\": %d,\n", n->attempt);
        strbuf_appendf(&sb, "      \"retries\": %d,\n", n->retries);
        strbuf_appendf(&sb, "      \"started_ms\": %ld,\n", n->started_ms);
        strbuf_appendf(&sb, "      \"finished_ms\": %ld,\n", n->finished_ms);
        strbuf_appendf(&sb, "      \"output\": ");
        if (n->output)
            _append_json_string(&sb, n->output);
        else
            strbuf_append_str(&sb, "null");
        strbuf_appendf(&sb, ",\n");
        strbuf_appendf(&sb, "      \"ndeps\": %d", n->ndeps);
        if (n->ndeps > 0) {
            strbuf_appendf(&sb, ",\n      \"deps\": [");
            for (int d = 0; d < n->ndeps; d++) {
                if (d)
                    strbuf_appendf(&sb, ", ");
                _append_json_string(&sb, n->deps[d]);
            }
            strbuf_appendf(&sb, "]");
        }
        strbuf_appendf(&sb, "\n    }%s\n", i < p->nnodes - 1 ? "," : "");
    }
    strbuf_appendf(&sb, "  ],\n");

    strbuf_appendf(&sb, "  \"cycles\": {\n");
    strbuf_appendf(&sb, "    \"nedges\": %d,\n", o->cycles.nedges);
    strbuf_appendf(&sb, "    \"max_iterations\": %d,\n", o->cycles.max_iterations);
    strbuf_appendf(&sb, "    \"edges\": [\n");
    for (int i = 0; i < o->cycles.nedges; i++) {
        const rcycle_edge_t *e = &o->cycles.edges[i];
        strbuf_appendf(&sb, "      {\"tail\": ");
        _append_json_string(&sb, e->tail_id);
        strbuf_appendf(&sb, ", \"targets\": [");
        for (int t = 0; t < e->ntargets; t++) {
            if (t)
                strbuf_appendf(&sb, ", ");
            _append_json_string(&sb, e->targets[t]);
        }
        strbuf_appendf(&sb, "], \"iteration\": %d}%s\n", o->cycles.iteration_counts[i],
                       i < o->cycles.nedges - 1 ? "," : "");
    }
    strbuf_appendf(&sb, "    ]\n");
    strbuf_appendf(&sb, "  },\n");

    strbuf_appendf(&sb, "  \"criteria\": [\n");
    for (int i = 0; i < o->ncriteria; i++) {
        const orch_criteria_entry_t *ce = &o->criteria_map[i];
        strbuf_appendf(&sb, "    {\n");
        strbuf_appendf(&sb, "      \"node\": ");
        _append_json_string(&sb, ce->node_id);
        strbuf_appendf(&sb, ",\n");
        strbuf_appendf(&sb, "      \"rules\": [\n");
        for (int r = 0; r < ce->rules.nrules; r++) {
            const sc_rule_t *rule = &ce->rules.rules[r];
            strbuf_appendf(&sb, "        {\"kind\": ");
            _append_json_string(&sb, sc_kind_name(rule->kind));
            strbuf_appendf(&sb, ", \"value\": ");
            _append_json_string(&sb, rule->value);
            strbuf_appendf(&sb, ", \"path\": ");
            _append_json_string(&sb, rule->path);
            strbuf_appendf(&sb, ", \"case_sensitive\": %s}%s\n",
                           rule->case_sensitive ? "true" : "false",
                           r < ce->rules.nrules - 1 ? "," : "");
        }
        strbuf_appendf(&sb, "      ]\n");
        strbuf_appendf(&sb, "    }%s\n", i < o->ncriteria - 1 ? "," : "");
    }
    strbuf_appendf(&sb, "  ],\n");

    strbuf_appendf(&sb, "  \"probes\": [\n");
    for (int i = 0; i < o->nprobes; i++) {
        probe_snapshot_t probe_snap;
        probe_snapshot(&o->probes[i], &probe_snap);
        strbuf_appendf(&sb, "    {\n");
        strbuf_appendf(&sb, "      \"group\": ");
        _append_json_string(&sb, probe_snap.watch_group);
        strbuf_appendf(&sb, ",\n");
        strbuf_appendf(&sb, "      \"interval_s\": %d,\n", probe_snap.interval_s);
        strbuf_appendf(&sb, "      \"tick_count\": %d,\n", probe_snap.tick_count);
        strbuf_appendf(&sb, "      \"state\": %d,\n", (int)probe_snap.state);
        strbuf_appendf(&sb, "      \"last_tick_age_ms\": %ld,\n", probe_snap.last_tick_age_ms);
        strbuf_appendf(&sb, "      \"next_tick_delay_ms\": %ld,\n", probe_snap.next_tick_delay_ms);
        strbuf_appendf(&sb, "      \"actions\": [\n");
        for (int a = 0; a < probe_snap.nactions; a++) {
            const probe_action_t *act = &probe_snap.actions[a];
            strbuf_appendf(&sb, "        {\"kind\": %d, \"target\": ", (int)act->kind);
            _append_json_string(&sb, act->target_id);
            strbuf_appendf(&sb, ", \"reason\": ");
            _append_json_string(&sb, act->reason);
            strbuf_appendf(&sb, "}%s\n", a < probe_snap.nactions - 1 ? "," : "");
        }
        strbuf_appendf(&sb, "      ]\n");
        strbuf_appendf(&sb, "    }%s\n", i < o->nprobes - 1 ? "," : "");
    }
    strbuf_appendf(&sb, "  ],\n");

    strbuf_appendf(&sb, "  \"watchdog\": {\n");
    strbuf_appendf(&sb, "    \"deadline_ms\": %d,\n", wd.deadline_ms);
    strbuf_appendf(&sb, "    \"total_turns\": %d,\n", wd.total_turns);
    strbuf_appendf(&sb, "    \"successful_turns\": %d,\n", wd.successful_turns);
    strbuf_appendf(&sb, "    \"timeout_count\": %d,\n", wd.timeout_count);
    strbuf_appendf(&sb, "    \"consecutive_timeouts\": %d,\n", wd.consecutive_timeouts);
    strbuf_appendf(&sb, "    \"longest_turn_ms\": %ld\n", wd.longest_turn_ms);
    strbuf_appendf(&sb, "  },\n");

    strbuf_appendf(&sb, "  \"failure_router\": {\n");
    strbuf_appendf(&sb, "    \"classes\": [\n");
    for (int i = 0; i < FAIL_CLASS_COUNT; i++) {
        strbuf_appendf(&sb, "      {\"cls\": %d, \"head\": %d, \"count\": %d, \"timestamps\": [", i,
                       fr.classes[i].head, fr.classes[i].count);
        for (int j = 0; j < FR_WINDOW_SIZE; j++) {
            if (j)
                strbuf_appendf(&sb, ", ");
            strbuf_appendf(&sb, "%ld", fr.classes[i].timestamps[j]);
        }
        strbuf_appendf(&sb, "]}%s\n", i < (FAIL_CLASS_COUNT - 1) ? "," : "");
    }
    strbuf_appendf(&sb, "    ]\n");
    strbuf_appendf(&sb, "  },\n");

    strbuf_appendf(&sb, "  \"stall_detector\": {\n");
    strbuf_appendf(&sb, "    \"total_turns\": %d,\n", stall.total_turns);
    strbuf_appendf(&sb, "    \"last_progress_age_s\": %ld,\n", stall.last_progress_age_s);
    strbuf_appendf(&sb, "    \"window\": [\n");
    for (int i = 0; i < stall.window_count; i++) {
        const stall_turn_snapshot_t *turn = &stall.window[i];
        strbuf_appendf(&sb, "      {\"tool_signature\": ");
        _append_json_string(&sb, turn->tool_signature);
        strbuf_appendf(&sb, ", \"output_chars\": %d, \"files_changed\": %s, \"age_s\": %ld}%s\n",
                       turn->output_chars, turn->files_changed ? "true" : "false", turn->age_s,
                       i < stall.window_count - 1 ? "," : "");
    }
    strbuf_appendf(&sb, "    ],\n");
    strbuf_appendf(&sb, "    \"read_freq\": [\n");
    for (int i = 0; i < stall.read_freq_count; i++) {
        strbuf_appendf(&sb, "      {\"hash\": %lu, \"count\": %d}%s\n", stall.reads[i].hash,
                       stall.reads[i].count, i < stall.read_freq_count - 1 ? "," : "");
    }
    strbuf_appendf(&sb, "    ]\n");
    strbuf_appendf(&sb, "  },\n");

    strbuf_appendf(&sb, "  \"context_fatigue\": {\n");
    strbuf_appendf(&sb, "    \"count\": %d,\n", fatigue.count);
    strbuf_appendf(&sb, "    \"context_limit\": %d,\n", fatigue.context_limit);
    strbuf_appendf(&sb, "    \"ema_velocity\": %.6f,\n", fatigue.ema_velocity);
    strbuf_appendf(&sb, "    \"last_total\": %d,\n", fatigue.last_total);
    strbuf_appendf(&sb, "    \"initialized\": %s,\n", fatigue.initialized ? "true" : "false");
    strbuf_appendf(&sb, "    \"samples\": [\n");
    for (int i = 0; i < fatigue.count; i++) {
        const fatigue_sample_snapshot_t *sample = &fatigue.samples[i];
        strbuf_appendf(&sb,
                       "      {\"turn\": %d, \"tokens_used\": %d, \"tokens_added\": %d, "
                       "\"age_s\": %ld}%s\n",
                       sample->turn, sample->tokens_used, sample->tokens_added, sample->age_s,
                       i < fatigue.count - 1 ? "," : "");
    }
    strbuf_appendf(&sb, "    ]\n");
    strbuf_appendf(&sb, "  },\n");

    strbuf_appendf(&sb, "  \"output_eval\": {\n");
    strbuf_appendf(&sb, "    \"total_evals\": %d,\n", oeval.total_evals);
    strbuf_appendf(&sb, "    \"pass_count\": %d,\n", oeval.pass_count);
    strbuf_appendf(&sb, "    \"weak_count\": %d,\n", oeval.weak_count);
    strbuf_appendf(&sb, "    \"incomplete_count\": %d,\n", oeval.incomplete_count);
    strbuf_appendf(&sb, "    \"off_topic_count\": %d,\n", oeval.off_topic_count);
    strbuf_appendf(&sb, "    \"empty_count\": %d,\n", oeval.empty_count);
    strbuf_appendf(&sb, "    \"consecutive_weak\": %d\n", oeval.consecutive_weak);
    strbuf_appendf(&sb, "  },\n");

    strbuf_appendf(&sb, "  \"auto_test\": {\n");
    strbuf_appendf(&sb, "    \"count\": %d,\n", atest.count);
    strbuf_appendf(&sb, "    \"root\": ");
    _append_json_string(&sb, atest.root);
    strbuf_appendf(&sb, ",\n");
    strbuf_appendf(&sb, "    \"last_turn\": %d,\n", atest.last_turn);
    strbuf_appendf(&sb, "    \"total_runs\": %d,\n", atest.total_runs);
    strbuf_appendf(&sb, "    \"total_pass\": %d,\n", atest.total_pass);
    strbuf_appendf(&sb, "    \"total_fail\": %d,\n", atest.total_fail);
    strbuf_appendf(&sb, "    \"last_age_s\": %ld,\n", atest.last_age_s);
    strbuf_appendf(&sb, "    \"enabled\": %s,\n", atest.enabled ? "true" : "false");
    strbuf_appendf(&sb, "    \"patterns\": [\n");
    for (int i = 0; i < atest.count; i++) {
        const auto_test_pattern_snapshot_t *pattern = &atest.patterns[i];
        strbuf_appendf(&sb, "      {\"pattern\": ");
        _append_json_string(&sb, pattern->pattern);
        strbuf_appendf(&sb, ", \"command\": ");
        _append_json_string(&sb, pattern->command);
        strbuf_appendf(&sb, ", \"detected\": %s}%s\n", pattern->detected ? "true" : "false",
                       i < atest.count - 1 ? "," : "");
    }
    strbuf_appendf(&sb, "    ]\n");
    strbuf_appendf(&sb, "  },\n");

    strbuf_appendf(&sb, "  \"checkpoint_discipline\": {\n");
    strbuf_appendf(&sb, "    \"count\": %d,\n", checkpoint.count);
    strbuf_appendf(&sb, "    \"interval\": %d,\n", checkpoint.interval);
    strbuf_appendf(&sb, "    \"turns_since_cp\": %d,\n", checkpoint.turns_since_cp);
    strbuf_appendf(&sb, "    \"consecutive_failures\": %d,\n", checkpoint.consecutive_failures);
    strbuf_appendf(&sb, "    \"active\": %s,\n", checkpoint.active ? "true" : "false");
    strbuf_appendf(&sb, "    \"session_id\": ");
    _append_json_string(&sb, checkpoint.session_id);
    strbuf_appendf(&sb, ",\n");
    strbuf_appendf(&sb, "    \"entries\": [\n");
    for (int i = 0; i < checkpoint.count; i++) {
        const checkpoint_entry_snapshot_t *entry = &checkpoint.entries[i];
        strbuf_appendf(&sb, "      {\"turn\": %d, \"age_s\": %ld, \"tag\": ", entry->turn,
                       entry->age_s);
        _append_json_string(&sb, entry->tag);
        strbuf_appendf(&sb, "}%s\n", i < checkpoint.count - 1 ? "," : "");
    }
    strbuf_appendf(&sb, "    ]\n");
    strbuf_appendf(&sb, "  },\n");

    strbuf_appendf(&sb, "  \"tool_verify\": {\n");
    strbuf_appendf(&sb, "    \"count\": %d,\n", tv.count);
    strbuf_appendf(&sb, "    \"total_verified\": %d,\n", tv.total_verified);
    strbuf_appendf(&sb, "    \"total_flagged\": %d,\n", tv.total_flagged);
    strbuf_appendf(&sb, "    \"consecutive_empty\": %d,\n", tv.consecutive_empty);
    strbuf_appendf(&sb, "    \"records\": [\n");
    for (int i = 0; i < tv.count; i++) {
        const tool_verify_record_snapshot_t *rec = &tv.records[i];
        strbuf_appendf(&sb, "      {\"tool\": ");
        _append_json_string(&sb, rec->tool);
        strbuf_appendf(&sb, ", \"output_len\": %d, \"flagged\": %s, \"age_s\": %ld}%s\n",
                       rec->output_len, rec->flagged ? "true" : "false", rec->age_s,
                       i < tv.count - 1 ? "," : "");
    }
    strbuf_appendf(&sb, "    ]\n");
    strbuf_appendf(&sb, "  },\n");

    strbuf_appendf(&sb, "  \"doom_loop\": {\n");
    strbuf_appendf(&sb, "    \"enabled\": %s,\n", doom.enabled ? "true" : "false");
    strbuf_appendf(&sb, "    \"count\": %d,\n", doom.count);
    strbuf_appendf(&sb, "    \"fire_count\": %d,\n", doom.fire_count);
    strbuf_appendf(&sb, "    \"entries\": [\n");
    for (int i = 0; i < doom.count; i++) {
        const doom_loop_entry_snapshot_t *entry = &doom.entries[i];
        strbuf_appendf(&sb, "      {\"hash\": %u, \"tool\": ", (unsigned)entry->hash);
        _append_json_string(&sb, entry->tool_name);
        strbuf_appendf(&sb, "}%s\n", i < doom.count - 1 ? "," : "");
    }
    strbuf_appendf(&sb, "    ]\n");
    strbuf_appendf(&sb, "  },\n");

    strbuf_appendf(&sb, "  \"shared_pad\": {\n");
    strbuf_appendf(&sb, "    \"count\": %d,\n", pad.count);
    strbuf_appendf(&sb, "    \"persistent\": %s,\n", pad.persistent ? "true" : "false");
    strbuf_appendf(&sb, "    \"path\": ");
    _append_json_string(&sb, pad.path);
    strbuf_appendf(&sb, ",\n");
    strbuf_appendf(&sb, "    \"run_token\": ");
    _append_json_string(&sb, pad.run_token);
    strbuf_appendf(&sb, ",\n");
    strbuf_appendf(&sb, "    \"lines\": [\n");
    for (int i = 0; i < pad.count; i++) {
        strbuf_appendf(&sb, "      {\"author\": ");
        _append_json_string(&sb, pad.lines[i].author);
        strbuf_appendf(&sb, ", \"text\": ");
        _append_json_string(&sb, pad.lines[i].text);
        strbuf_appendf(&sb, ", \"age_s\": %ld}%s\n", pad.lines[i].age_s,
                       i < pad.count - 1 ? "," : "");
    }
    strbuf_appendf(&sb, "    ]\n");
    strbuf_appendf(&sb, "  },\n");

    strbuf_appendf(&sb, "  \"ledger_event_count\": %d,\n", ledger_event_count(&o->ledger));
    strbuf_appendf(&sb, "  \"ledger_events\": [\n");
    for (int i = 0; i < o->ledger.nevents; i++) {
        const ledger_event_t *ev = &o->ledger.events[i];
        strbuf_appendf(&sb, "    {\"seq\": %d, \"ts\": %ld, \"type\": ", ev->sequence,
                       ev->timestamp);
        _append_json_string(&sb, ev->type);
        if (ev->node_id[0]) {
            strbuf_appendf(&sb, ", \"node\": ");
            _append_json_string(&sb, ev->node_id);
        }
        if (ev->detail[0]) {
            strbuf_appendf(&sb, ", \"detail\": ");
            _append_json_string(&sb, ev->detail);
        }
        if (ev->has_output) {
            strbuf_appendf(&sb, ", \"output\": ");
            _append_json_string(&sb, ev->output);
        }
        if (ev->has_exit_code)
            strbuf_appendf(&sb, ", \"exit_code\": %d", ev->exit_code);
        if (ev->has_run_token) {
            strbuf_appendf(&sb, ", \"run_token\": ");
            _append_json_string(&sb, ev->run_token);
        }
        strbuf_appendf(&sb, "}%s\n", i < o->ledger.nevents - 1 ? "," : "");
    }
    strbuf_appendf(&sb, "  ],\n");

    strbuf_appendf(&sb, "  \"cancel_flags\": [");
    for (int i = 0; i < o->ncancel_flags; i++) {
        if (i)
            strbuf_appendf(&sb, ", ");
        _append_json_string(&sb, o->cancel_flags[i]);
    }
    strbuf_appendf(&sb, "],\n");

    strbuf_appendf(&sb, "  \"rerun_queue\": [");
    for (int i = 0; i < o->nrerun_queued; i++) {
        if (i)
            strbuf_appendf(&sb, ", ");
        _append_json_string(&sb, o->rerun_queue[i]);
    }
    strbuf_appendf(&sb, "]\n");

    strbuf_appendf(&sb, "}\n");

    return strbuf_detach(&sb);
}

int run_persist_save(const pipe_orch_t *o, const char *path) {
    if (!o || !path)
        return -1;

    char *json = run_persist_snapshot_json(o);
    if (!json)
        return -1;

    int rc = nc_write_file_atomic(path, json, strlen(json));
    if (rc == 0 && o->pad.persistent && o->pad.path[0])
        rc = shared_pad_persist((shared_pad_t *)&o->pad);
    if (rc == 0 && o->ledger.persistent && o->ledger.snap_path[0])
        rc = ledger_write_snapshot((event_ledger_t *)&o->ledger, json);
    if (rc == 0 && o->ledger.persistent && o->ledger.log_path[0])
        rc = ledger_flush_log((event_ledger_t *)&o->ledger);
    free(json);
    return rc;
}

static pipe_status_t _parse_status(const char *s) {
    if (!s)
        return PIPE_PENDING;
    if (strcmp(s, "pending") == 0)
        return PIPE_PENDING;
    if (strcmp(s, "queued") == 0)
        return PIPE_QUEUED;
    if (strcmp(s, "running") == 0)
        return PIPE_RUNNING;
    if (strcmp(s, "done") == 0)
        return PIPE_DONE;
    if (strcmp(s, "failed") == 0)
        return PIPE_FAILED;
    if (strcmp(s, "skipped") == 0)
        return PIPE_SKIPPED;
    if (strcmp(s, "cancelled") == 0)
        return PIPE_CANCELLED;
    return PIPE_PENDING;
}

static const char *_find_key(const char *json, const char *json_end, const char *key) {
    char pattern[NC_TAG_MAX];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    size_t plen = strlen(pattern);
    const char *limit = json_end ? json_end : json + strlen(json);

    const char *p = json;
    while (p < limit) {
        p = strstr(p, pattern);
        if (!p || p + plen > limit)
            return NULL;
        const char *val = p + plen;
        while (val < limit && (*val == ' ' || *val == '\t'))
            val++;
        return val;
    }
    return NULL;
}

static char *_extract_str_in(const char *json, const char *json_end, const char *key) {
    const char *limit = json_end ? json_end : json + strlen(json);
    const char *val = _find_key(json, json_end, key);
    if (!val)
        return NULL;
    if (*val != '"')
        return NULL;
    return _dup_json_string_token(&val, limit);
}

static char *_extract_str(const char *json, const char *key) {
    return _extract_str_in(json, NULL, key);
}

static long _extract_long_in(const char *json, const char *json_end, const char *key) {
    const char *val = _find_key(json, json_end, key);
    if (!val)
        return 0;
    return nc_safe_atol(val, 0);
}

static long _extract_long(const char *json, const char *key) {
    return _extract_long_in(json, NULL, key);
}

static int _extract_int_in(const char *json, const char *json_end, const char *key) {
    return (int)_extract_long_in(json, json_end, key);
}

static int _extract_int(const char *json, const char *key) {
    return (int)_extract_long(json, key);
}

static bool _snapshot_json_meta(const char *json, long *timestamp, int *ledger_events,
                                char *identity_token, size_t identity_token_sz) {
    if (timestamp)
        *timestamp = 0;
    if (ledger_events)
        *ledger_events = 0;
    if (identity_token && identity_token_sz > 0)
        identity_token[0] = '\0';
    if (!json)
        return false;

    char *magic = _extract_str(json, "magic");
    bool ok = magic && strcmp(magic, RUN_PERSIST_MAGIC) == 0;
    free(magic);
    if (!ok)
        return false;

    int ver = _extract_int(json, "version");
    if (ver < 1 || ver > RUN_PERSIST_VERSION)
        return false;

    if (timestamp)
        *timestamp = _extract_long(json, "timestamp");
    if (ledger_events)
        *ledger_events = _extract_int(json, "ledger_event_count");
    if (identity_token && identity_token_sz > 0) {
        char *tok = _extract_str(json, "run_scope_token");
        if (tok && tok[0]) {
            nc_strlcpy(identity_token, tok, identity_token_sz);
            free(tok);
        } else {
            free(tok);
            const char *pipeline_end = NULL;
            const char *pipeline_section =
                _find_compound_in(json, NULL, "pipeline", '{', '}', &pipeline_end);
            char *name =
                pipeline_section ? _extract_str_in(pipeline_section, pipeline_end, "name") : NULL;
            if (name) {
                nc_strlcpy(identity_token, name, identity_token_sz);
                free(name);
            }
        }
    }
    return true;
}

static bool _snapshot_mtime(const char *path, long *sec_out, long *nsec_out) {
    if (sec_out)
        *sec_out = 0;
    if (nsec_out)
        *nsec_out = 0;
    if (!path || !path[0])
        return false;

    struct stat st;
    if (stat(path, &st) != 0)
        return false;

#if defined(__APPLE__)
    if (sec_out)
        *sec_out = (long)st.st_mtimespec.tv_sec;
    if (nsec_out)
        *nsec_out = (long)st.st_mtimespec.tv_nsec;
#else
    if (sec_out)
        *sec_out = (long)st.st_mtim.tv_sec;
    if (nsec_out)
        *nsec_out = (long)st.st_mtim.tv_nsec;
#endif
    return true;
}

static bool _snapshot_candidate_is_fresher(const char *current_json, const char *current_path,
                                           const char *candidate_json, const char *candidate_path) {
    long current_ts = 0, candidate_ts = 0;
    int current_events = 0, candidate_events = 0;
    char current_identity[CSCOPE_MAX_TOKENS];
    char candidate_identity[CSCOPE_MAX_TOKENS];
    bool current_ok = _snapshot_json_meta(current_json, &current_ts, &current_events,
                                          current_identity, sizeof(current_identity));
    bool candidate_ok = _snapshot_json_meta(candidate_json, &candidate_ts, &candidate_events,
                                            candidate_identity, sizeof(candidate_identity));
    if (!candidate_ok)
        return false;
    if (!current_ok)
        return true;
    if (current_identity[0] && candidate_identity[0] &&
        strcmp(current_identity, candidate_identity) != 0)
        return false;

    long current_msec = 0, current_mnsec = 0;
    long candidate_msec = 0, candidate_mnsec = 0;
    bool current_mtime = _snapshot_mtime(current_path, &current_msec, &current_mnsec);
    bool candidate_mtime = _snapshot_mtime(candidate_path, &candidate_msec, &candidate_mnsec);
    if (current_mtime && candidate_mtime) {
        if (candidate_msec != current_msec)
            return candidate_msec > current_msec;
        if (candidate_mnsec != current_mnsec)
            return candidate_mnsec > current_mnsec;
    }

    if (candidate_ts != current_ts)
        return candidate_ts > current_ts;
    if (candidate_events != current_events)
        return candidate_events > current_events;
    return false;
}

static char *_load_preferred_snapshot_json(const pipe_orch_t *o, const char *path) {
    if (!path)
        return NULL;

    char *primary = strbuf_read_file_max(path, NULL, NC_READ_CAP_HARNESS);
    if (!primary)
        return NULL;

    if (!o || !o->ledger.persistent || !o->ledger.snap_path[0] ||
        strcmp(path, o->ledger.snap_path) == 0)
        return primary;

    char *sidecar = strbuf_read_file_max(o->ledger.snap_path, NULL, NC_READ_CAP_HARNESS);
    if (!sidecar)
        return primary;

    if (_snapshot_candidate_is_fresher(primary, path, sidecar, o->ledger.snap_path)) {
        free(primary);
        return sidecar;
    }

    free(sidecar);
    return primary;
}

static const char *_find_section(const char *json, const char *key, const char **out_end) {
    return _find_compound_in(json, NULL, key, '{', '}', out_end);
}

static void _restore_string_array(char (*dst)[PIPE_ID_MAX], int max_entries, size_t entry_sz,
                                  int *count, const char *json, const char *key) {
    const char *arr_end = NULL;
    const char *arr = _find_array(json, key, &arr_end);
    if (!arr || !arr_end || !dst || !count || max_entries <= 0 || entry_sz == 0)
        return;

    memset(dst, 0, (size_t)max_entries * entry_sz);
    *count = 0;
    const char *cursor = arr + 1;
    while (cursor < arr_end && *count < max_entries) {
        cursor = _skip_ws(cursor, arr_end);
        if (!cursor || cursor >= arr_end || *cursor == ']')
            break;
        if (*cursor != '"')
            break;

        const char *qcur = cursor;
        char *entry = _dup_json_string_token(&qcur, arr_end);
        if (!entry)
            break;

        size_t len = strlen(entry);
        if (len >= entry_sz)
            len = entry_sz - 1;
        memcpy(dst[*count], entry, len);
        dst[*count][len] = '\0';
        (*count)++;
        free(entry);

        cursor = qcur;
        while (cursor < arr_end && *cursor != ',' && *cursor != ']')
            cursor++;
        if (cursor < arr_end && *cursor == ',')
            cursor++;
    }
}

static void _restore_string_array_in(char (*dst)[PIPE_ID_MAX], int max_entries, size_t entry_sz,
                                     int *count, const char *json, const char *json_end,
                                     const char *key) {
    const char *arr_end = NULL;
    const char *arr = _find_array_in(json, json_end, key, &arr_end);
    if (!arr || !arr_end || !dst || !count || max_entries <= 0 || entry_sz == 0)
        return;

    memset(dst, 0, (size_t)max_entries * entry_sz);
    *count = 0;
    const char *cursor = arr + 1;
    while (cursor < arr_end && *count < max_entries) {
        cursor = _skip_ws(cursor, arr_end);
        if (!cursor || cursor >= arr_end || *cursor == ']')
            break;
        if (*cursor != '"')
            break;

        const char *qcur = cursor;
        char *entry = _dup_json_string_token(&qcur, arr_end);
        if (!entry)
            break;

        size_t len = strlen(entry);
        if (len >= entry_sz)
            len = entry_sz - 1;
        memcpy(dst[*count], entry, len);
        dst[*count][len] = '\0';
        (*count)++;
        free(entry);

        cursor = qcur;
        while (cursor < arr_end && *cursor != ',' && *cursor != ']')
            cursor++;
        if (cursor < arr_end && *cursor == ',')
            cursor++;
    }
}

static void _restore_cycle_edges(pipe_orch_t *o, const char *json, int snapshot_version) {
    const char *cycles_end = NULL;
    const char *cycles_section = _find_section(json, "cycles", &cycles_end);
    if (!cycles_section || !cycles_end)
        return;

    int max_iterations = _extract_int_in(cycles_section, cycles_end, "max_iterations");
    if (max_iterations <= 0)
        max_iterations = RCYCLE_DEFAULT_MAX_ITERS;

    const char *arr_end = NULL;
    const char *arr = _find_array_in(cycles_section, cycles_end, "edges", &arr_end);
    if (!arr || !arr_end) {
        o->cycles.max_iterations = max_iterations;
        return;
    }

    if (snapshot_version < 2) {
        o->cycles.max_iterations = max_iterations;
        const char *cursor = arr + 1;
        while (cursor && cursor < arr_end) {
            const char *obj = strchr(cursor, '{');
            if (!obj || obj >= arr_end)
                break;
            const char *obj_end = _scan_balanced(obj, arr_end, '{', '}');
            if (!obj_end)
                break;

            size_t chunk_len = (size_t)(obj_end - obj);
            char *chunk = malloc(chunk_len + 1);
            if (!chunk)
                break;
            memcpy(chunk, obj, chunk_len);
            chunk[chunk_len] = '\0';

            char *tail = _extract_str_in(chunk, chunk + chunk_len, "tail");
            int iteration = _extract_int_in(chunk, chunk + chunk_len, "iteration");

            if (tail) {
                for (int i = 0; i < o->cycles.nedges; i++) {
                    if (strcmp(o->cycles.edges[i].tail_id, tail) == 0) {
                        o->cycles.iteration_counts[i] = iteration;
                        break;
                    }
                }
            }
            free(tail);
            free(chunk);
            cursor = obj_end;
        }
        return;
    }

    retry_cycle_t restored;
    rcycle_init(&restored, max_iterations);

    const char *cursor = arr + 1;
    while (cursor && cursor < arr_end && restored.nedges < RCYCLE_MAX_EDGES) {
        const char *obj = strchr(cursor, '{');
        if (!obj || obj >= arr_end)
            break;
        const char *obj_end = _scan_balanced(obj, arr_end, '{', '}');
        if (!obj_end)
            break;

        size_t chunk_len = (size_t)(obj_end - obj);
        char *chunk = malloc(chunk_len + 1);
        if (!chunk)
            break;
        memcpy(chunk, obj, chunk_len);
        chunk[chunk_len] = '\0';

        char *tail = _extract_str_in(chunk, chunk + chunk_len, "tail");
        int iteration = _extract_int_in(chunk, chunk + chunk_len, "iteration");
        char targets[RCYCLE_MAX_CHAIN][PIPE_ID_MAX];
        int ntargets = 0;
        _restore_string_array_in(targets, RCYCLE_MAX_CHAIN, sizeof(targets[0]), &ntargets, chunk,
                                 chunk + chunk_len, "targets");

        if (tail && ntargets > 0) {
            const char *target_ids[RCYCLE_MAX_CHAIN];
            for (int i = 0; i < ntargets; i++)
                target_ids[i] = targets[i];
            if (rcycle_add_edge(&restored, tail, target_ids, ntargets) == 0) {
                restored.iteration_counts[restored.nedges - 1] = iteration;
            }
        }
        free(tail);
        free(chunk);
        cursor = obj_end;
    }

    o->cycles = restored;
}

static void _restore_probe_actions(periodic_probe_t *pp, const char *json, const char *json_end) {
    const char *arr_end = NULL;
    const char *arr = _find_array_in(json, json_end, "actions", &arr_end);
    if (!arr || !arr_end)
        return;

    pp->nactions = 0;
    const char *cursor = arr + 1;
    while (cursor && cursor < arr_end && pp->nactions < PROBE_MAX_ACTIONS) {
        const char *obj = strchr(cursor, '{');
        if (!obj || obj >= arr_end)
            break;
        const char *obj_end = _scan_balanced(obj, arr_end, '{', '}');
        if (!obj_end)
            break;

        size_t chunk_len = (size_t)(obj_end - obj);
        char *chunk = malloc(chunk_len + 1);
        if (!chunk)
            break;
        memcpy(chunk, obj, chunk_len);
        chunk[chunk_len] = '\0';

        probe_action_t *act = &pp->actions[pp->nactions];
        memset(act, 0, sizeof(*act));

        int kind = _extract_int_in(chunk, chunk + chunk_len, "kind");
        if (kind >= PROBE_ACT_NONE && kind <= PROBE_ACT_RESTART)
            act->kind = (probe_action_kind_t)kind;

        char *target = _extract_str_in(chunk, chunk + chunk_len, "target");
        char *reason = _extract_str_in(chunk, chunk + chunk_len, "reason");
        if (target)
            nc_strlcpy(act->target_id, target, sizeof(act->target_id));
        if (reason)
            nc_strlcpy(act->reason, reason, sizeof(act->reason));
        pp->nactions++;

        free(target);
        free(reason);
        free(chunk);
        cursor = obj_end;
    }
}

static void _restore_probes(pipe_orch_t *o, const char *json) {
    const char *arr_end = NULL;
    const char *arr = _find_array(json, "probes", &arr_end);
    if (!arr || !arr_end)
        return;

    o->nprobes = 0;
    const char *cursor = arr + 1;
    while (cursor && cursor < arr_end && o->nprobes < PIPE_ORCH_MAX_PROBES) {
        const char *obj = strchr(cursor, '{');
        if (!obj || obj >= arr_end)
            break;
        const char *obj_end = _scan_balanced(obj, arr_end, '{', '}');
        if (!obj_end)
            break;

        size_t chunk_len = (size_t)(obj_end - obj);
        char *chunk = malloc(chunk_len + 1);
        if (!chunk)
            break;
        memcpy(chunk, obj, chunk_len);
        chunk[chunk_len] = '\0';

        char *group = _extract_str_in(chunk, chunk + chunk_len, "group");
        int interval_s = _extract_int_in(chunk, chunk + chunk_len, "interval_s");
        if (group && pipe_orch_add_probe(o, group, interval_s) == 0) {
            periodic_probe_t *pp = &o->probes[o->nprobes - 1];
            probe_snapshot_t snap;
            memset(&snap, 0, sizeof(snap));
            nc_strlcpy(snap.watch_group, group, sizeof(snap.watch_group));
            snap.interval_s = interval_s;
            snap.tick_count = _extract_int_in(chunk, chunk + chunk_len, "tick_count");
            int state = _extract_int_in(chunk, chunk + chunk_len, "state");
            if (state >= PROBE_IDLE && state <= PROBE_DONE)
                snap.state = (probe_state_t)state;
            if (_find_key(chunk, chunk + chunk_len, "last_tick_age_ms"))
                snap.last_tick_age_ms =
                    _extract_long_in(chunk, chunk + chunk_len, "last_tick_age_ms");
            if (_find_key(chunk, chunk + chunk_len, "next_tick_delay_ms"))
                snap.next_tick_delay_ms =
                    _extract_long_in(chunk, chunk + chunk_len, "next_tick_delay_ms");
            else
                snap.next_tick_delay_ms =
                    (long)(interval_s > 0 ? interval_s : PROBE_TICK_DEFAULT_S) * 1000L;
            probe_restore(pp, &snap);
            _restore_probe_actions(pp, chunk, chunk + chunk_len);
        }

        free(group);
        free(chunk);
        cursor = obj_end;
    }
}

static void _restore_criteria_rules(sc_ruleset_t *rs, const char *json, const char *json_end) {
    const char *arr_end = NULL;
    const char *arr = _find_array_in(json, json_end, "rules", &arr_end);
    if (!arr || !arr_end)
        return;

    const char *cursor = arr + 1;
    while (cursor && cursor < arr_end && rs->nrules < SC_MAX_RULES) {
        const char *obj = strchr(cursor, '{');
        if (!obj || obj >= arr_end)
            break;
        const char *obj_end = _scan_balanced(obj, arr_end, '{', '}');
        if (!obj_end)
            break;

        size_t chunk_len = (size_t)(obj_end - obj);
        char *chunk = malloc(chunk_len + 1);
        if (!chunk)
            break;
        memcpy(chunk, obj, chunk_len);
        chunk[chunk_len] = '\0';

        char *kind_s = _extract_str_in(chunk, chunk + chunk_len, "kind");
        int kind = _parse_sc_kind(kind_s);
        if (kind >= 0) {
            sc_rule_t *rule = &rs->rules[rs->nrules++];
            memset(rule, 0, sizeof(*rule));
            rule->kind = (sc_kind_t)kind;
            rule->case_sensitive = _extract_bool_in(chunk, chunk + chunk_len, "case_sensitive");

            char *value = _extract_str_in(chunk, chunk + chunk_len, "value");
            char *path = _extract_str_in(chunk, chunk + chunk_len, "path");
            if (value)
                nc_strlcpy(rule->value, value, sizeof(rule->value));
            if (path)
                nc_strlcpy(rule->path, path, sizeof(rule->path));
            free(value);
            free(path);
        }

        free(kind_s);
        free(chunk);
        cursor = obj_end;
    }
}

static void _restore_criteria(pipe_orch_t *o, const char *json) {
    const char *arr_end = NULL;
    const char *arr = _find_array(json, "criteria", &arr_end);
    if (!arr || !arr_end)
        return;

    o->ncriteria = 0;
    const char *cursor = arr + 1;
    while (cursor && cursor < arr_end && o->ncriteria < PIPE_ORCH_MAX_CRITERIA) {
        const char *obj = strchr(cursor, '{');
        if (!obj || obj >= arr_end)
            break;
        const char *obj_end = _scan_balanced(obj, arr_end, '{', '}');
        if (!obj_end)
            break;

        size_t chunk_len = (size_t)(obj_end - obj);
        char *chunk = malloc(chunk_len + 1);
        if (!chunk)
            break;
        memcpy(chunk, obj, chunk_len);
        chunk[chunk_len] = '\0';

        char *node = _extract_str_in(chunk, chunk + chunk_len, "node");
        sc_ruleset_t rs;
        sc_ruleset_init(&rs);
        _restore_criteria_rules(&rs, chunk, chunk + chunk_len);
        if (node && rs.nrules > 0)
            (void)pipe_orch_add_criteria(o, node, &rs);

        free(node);
        free(chunk);
        cursor = obj_end;
    }
}

static void _restore_long_array(long *dst, int max_entries, const char *json, const char *json_end,
                                const char *key) {
    const char *arr_end = NULL;
    const char *arr = _find_array_in(json, json_end, key, &arr_end);
    if (!arr || !arr_end || !dst || max_entries <= 0)
        return;

    memset(dst, 0, sizeof(*dst) * (size_t)max_entries);
    const char *cursor = arr + 1;
    int n = 0;
    while (cursor && cursor < arr_end && n < max_entries) {
        cursor = _skip_ws(cursor, arr_end);
        if (!cursor || cursor >= arr_end || *cursor == ']')
            break;
        char *endptr = NULL;
        long v = strtol(cursor, &endptr, 10);
        if (endptr == cursor)
            break;
        dst[n++] = v;
        cursor = endptr;
        while (cursor < arr_end && *cursor != ',' && *cursor != ']')
            cursor++;
        if (cursor < arr_end && *cursor == ',')
            cursor++;
    }
}

static void _restore_watchdog(const char *json) {
    const char *wd_end = NULL;
    const char *wd_section = _find_section(json, "watchdog", &wd_end);
    if (!wd_section || !wd_end)
        return;

    watchdog_snapshot_t wd;
    memset(&wd, 0, sizeof(wd));
    wd.deadline_ms = _extract_int_in(wd_section, wd_end, "deadline_ms");
    wd.total_turns = _extract_int_in(wd_section, wd_end, "total_turns");
    wd.successful_turns = _extract_int_in(wd_section, wd_end, "successful_turns");
    wd.timeout_count = _extract_int_in(wd_section, wd_end, "timeout_count");
    wd.consecutive_timeouts = _extract_int_in(wd_section, wd_end, "consecutive_timeouts");
    wd.longest_turn_ms = _extract_long_in(wd_section, wd_end, "longest_turn_ms");
    watchdog_restore(&wd);
}

static void _restore_failure_router(const char *json) {
    const char *fr_end = NULL;
    const char *fr_section = _find_section(json, "failure_router", &fr_end);
    if (!fr_section || !fr_end)
        return;

    const char *arr_end = NULL;
    const char *arr = _find_array_in(fr_section, fr_end, "classes", &arr_end);
    if (!arr || !arr_end)
        return;

    failure_router_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));

    const char *cursor = arr + 1;
    while (cursor && cursor < arr_end) {
        const char *obj = strchr(cursor, '{');
        if (!obj || obj >= arr_end)
            break;
        const char *obj_end = _scan_balanced(obj, arr_end, '{', '}');
        if (!obj_end)
            break;

        size_t chunk_len = (size_t)(obj_end - obj);
        char *chunk = malloc(chunk_len + 1);
        if (!chunk)
            break;
        memcpy(chunk, obj, chunk_len);
        chunk[chunk_len] = '\0';

        int cls = _extract_int_in(chunk, chunk + chunk_len, "cls");
        if (cls >= 0 && cls < 5) {
            snap.classes[cls].head = _extract_int_in(chunk, chunk + chunk_len, "head");
            snap.classes[cls].count = _extract_int_in(chunk, chunk + chunk_len, "count");
            _restore_long_array(snap.classes[cls].timestamps, FR_WINDOW_SIZE, chunk,
                                chunk + chunk_len, "timestamps");
        }

        free(chunk);
        cursor = obj_end;
    }

    failure_router_restore(&snap);
}

static void _restore_stall_detector(const char *json) {
    const char *stall_end = NULL;
    const char *stall_section = _find_section(json, "stall_detector", &stall_end);
    if (!stall_section || !stall_end) {
        stall_detector_restore(NULL);
        return;
    }

    stall_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.total_turns = _extract_int_in(stall_section, stall_end, "total_turns");
    snap.last_progress_age_s = _extract_long_in(stall_section, stall_end, "last_progress_age_s");

    const char *window_end = NULL;
    const char *window = _find_array_in(stall_section, stall_end, "window", &window_end);
    if (window && window_end) {
        const char *cursor = window + 1;
        while (cursor && cursor < window_end && snap.window_count < STALL_WINDOW_SIZE) {
            const char *obj = strchr(cursor, '{');
            if (!obj || obj >= window_end)
                break;
            const char *obj_end = _scan_balanced(obj, window_end, '{', '}');
            if (!obj_end)
                break;

            size_t chunk_len = (size_t)(obj_end - obj);
            char *chunk = malloc(chunk_len + 1);
            if (!chunk)
                break;
            memcpy(chunk, obj, chunk_len);
            chunk[chunk_len] = '\0';

            stall_turn_snapshot_t *turn = &snap.window[snap.window_count++];
            char *sig = _extract_str_in(chunk, chunk + chunk_len, "tool_signature");
            if (sig) {
                nc_strlcpy(turn->tool_signature, sig, sizeof(turn->tool_signature));
                free(sig);
            }
            turn->output_chars = _extract_int_in(chunk, chunk + chunk_len, "output_chars");
            turn->files_changed = _extract_bool_in(chunk, chunk + chunk_len, "files_changed");
            turn->age_s = _extract_long_in(chunk, chunk + chunk_len, "age_s");

            free(chunk);
            cursor = obj_end;
        }
    }

    const char *read_end = NULL;
    const char *read_arr = _find_array_in(stall_section, stall_end, "read_freq", &read_end);
    if (read_arr && read_end) {
        const char *cursor = read_arr + 1;
        while (cursor && cursor < read_end && snap.read_freq_count < STALL_READ_LOOP_WINDOW) {
            const char *obj = strchr(cursor, '{');
            if (!obj || obj >= read_end)
                break;
            const char *obj_end = _scan_balanced(obj, read_end, '{', '}');
            if (!obj_end)
                break;

            size_t chunk_len = (size_t)(obj_end - obj);
            char *chunk = malloc(chunk_len + 1);
            if (!chunk)
                break;
            memcpy(chunk, obj, chunk_len);
            chunk[chunk_len] = '\0';

            stall_read_snapshot_t *read = &snap.reads[snap.read_freq_count++];
            read->hash = _extract_ulong_in(chunk, chunk + chunk_len, "hash");
            read->count = _extract_int_in(chunk, chunk + chunk_len, "count");

            free(chunk);
            cursor = obj_end;
        }
    }

    stall_detector_restore(&snap);
}

static void _restore_context_fatigue(const char *json) {
    const char *fatigue_end = NULL;
    const char *fatigue_section = _find_section(json, "context_fatigue", &fatigue_end);
    if (!fatigue_section || !fatigue_end) {
        context_fatigue_restore(NULL);
        return;
    }

    context_fatigue_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.count = _extract_int_in(fatigue_section, fatigue_end, "count");
    snap.context_limit = _extract_int_in(fatigue_section, fatigue_end, "context_limit");
    snap.ema_velocity = _extract_double_in(fatigue_section, fatigue_end, "ema_velocity");
    snap.last_total = _extract_int_in(fatigue_section, fatigue_end, "last_total");
    snap.initialized = _extract_bool_in(fatigue_section, fatigue_end, "initialized");

    const char *samples_end = NULL;
    const char *samples = _find_array_in(fatigue_section, fatigue_end, "samples", &samples_end);
    if (samples && samples_end) {
        const char *cursor = samples + 1;
        int idx = 0;
        while (cursor && cursor < samples_end && idx < FATIGUE_SAMPLE_MAX) {
            const char *obj = strchr(cursor, '{');
            if (!obj || obj >= samples_end)
                break;
            const char *obj_end = _scan_balanced(obj, samples_end, '{', '}');
            if (!obj_end)
                break;

            size_t chunk_len = (size_t)(obj_end - obj);
            char *chunk = malloc(chunk_len + 1);
            if (!chunk)
                break;
            memcpy(chunk, obj, chunk_len);
            chunk[chunk_len] = '\0';

            snap.samples[idx].turn = _extract_int_in(chunk, chunk + chunk_len, "turn");
            snap.samples[idx].tokens_used =
                _extract_int_in(chunk, chunk + chunk_len, "tokens_used");
            snap.samples[idx].tokens_added =
                _extract_int_in(chunk, chunk + chunk_len, "tokens_added");
            snap.samples[idx].age_s = _extract_long_in(chunk, chunk + chunk_len, "age_s");

            free(chunk);
            cursor = obj_end;
            idx++;
        }
        snap.count = idx;
    }

    context_fatigue_restore(&snap);
}

static void _restore_output_eval(const char *json) {
    const char *oeval_end = NULL;
    const char *oeval_section = _find_section(json, "output_eval", &oeval_end);
    if (!oeval_section || !oeval_end) {
        output_eval_restore(NULL);
        return;
    }

    output_eval_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.total_evals = _extract_int_in(oeval_section, oeval_end, "total_evals");
    snap.pass_count = _extract_int_in(oeval_section, oeval_end, "pass_count");
    snap.weak_count = _extract_int_in(oeval_section, oeval_end, "weak_count");
    snap.incomplete_count = _extract_int_in(oeval_section, oeval_end, "incomplete_count");
    snap.off_topic_count = _extract_int_in(oeval_section, oeval_end, "off_topic_count");
    snap.empty_count = _extract_int_in(oeval_section, oeval_end, "empty_count");
    snap.consecutive_weak = _extract_int_in(oeval_section, oeval_end, "consecutive_weak");
    output_eval_restore(&snap);
}

static void _restore_auto_test(const char *json) {
    const char *atest_end = NULL;
    const char *atest_section = _find_section(json, "auto_test", &atest_end);
    if (!atest_section || !atest_end) {
        auto_test_restore(NULL);
        return;
    }

    auto_test_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    char *root = _extract_str_in(atest_section, atest_end, "root");
    if (root) {
        nc_strlcpy(snap.root, root, sizeof(snap.root));
        free(root);
    }
    snap.last_turn = _extract_int_in(atest_section, atest_end, "last_turn");
    snap.total_runs = _extract_int_in(atest_section, atest_end, "total_runs");
    snap.total_pass = _extract_int_in(atest_section, atest_end, "total_pass");
    snap.total_fail = _extract_int_in(atest_section, atest_end, "total_fail");
    snap.last_age_s = _extract_long_in(atest_section, atest_end, "last_age_s");
    snap.enabled = _extract_bool_in(atest_section, atest_end, "enabled");

    const char *patterns_end = NULL;
    const char *patterns = _find_array_in(atest_section, atest_end, "patterns", &patterns_end);
    if (patterns && patterns_end) {
        const char *cursor = patterns + 1;
        int idx = 0;
        while (cursor && cursor < patterns_end && idx < ATEST_MAX_PATTERNS) {
            const char *obj = strchr(cursor, '{');
            if (!obj || obj >= patterns_end)
                break;
            const char *obj_end = _scan_balanced(obj, patterns_end, '{', '}');
            if (!obj_end)
                break;

            size_t chunk_len = (size_t)(obj_end - obj);
            char *chunk = malloc(chunk_len + 1);
            if (!chunk)
                break;
            memcpy(chunk, obj, chunk_len);
            chunk[chunk_len] = '\0';

            char *pattern = _extract_str_in(chunk, chunk + chunk_len, "pattern");
            if (pattern) {
                nc_strlcpy(snap.patterns[idx].pattern, pattern, sizeof(snap.patterns[idx].pattern));
                free(pattern);
            }
            char *command = _extract_str_in(chunk, chunk + chunk_len, "command");
            if (command) {
                nc_strlcpy(snap.patterns[idx].command, command, sizeof(snap.patterns[idx].command));
                free(command);
            }
            snap.patterns[idx].detected = _extract_bool_in(chunk, chunk + chunk_len, "detected");

            free(chunk);
            cursor = obj_end;
            idx++;
        }
        snap.count = idx;
    }

    auto_test_restore(&snap);
}

static void _restore_checkpoint_discipline(const char *json) {
    const char *checkpoint_end = NULL;
    const char *checkpoint_section = _find_section(json, "checkpoint_discipline", &checkpoint_end);
    if (!checkpoint_section || !checkpoint_end) {
        checkpoint_discipline_restore_state(NULL);
        return;
    }

    checkpoint_discipline_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.interval = _extract_int_in(checkpoint_section, checkpoint_end, "interval");
    snap.turns_since_cp = _extract_int_in(checkpoint_section, checkpoint_end, "turns_since_cp");
    snap.consecutive_failures =
        _extract_int_in(checkpoint_section, checkpoint_end, "consecutive_failures");
    snap.active = _extract_bool_in(checkpoint_section, checkpoint_end, "active");
    char *session_id = _extract_str_in(checkpoint_section, checkpoint_end, "session_id");
    if (session_id) {
        nc_strlcpy(snap.session_id, session_id, sizeof(snap.session_id));
        free(session_id);
    }

    const char *entries_end = NULL;
    const char *entries =
        _find_array_in(checkpoint_section, checkpoint_end, "entries", &entries_end);
    if (entries && entries_end) {
        const char *cursor = entries + 1;
        int idx = 0;
        while (cursor && cursor < entries_end && idx < NC_CHECKPOINT_MAX_ENTRIES) {
            const char *obj = strchr(cursor, '{');
            if (!obj || obj >= entries_end)
                break;
            const char *obj_end = _scan_balanced(obj, entries_end, '{', '}');
            if (!obj_end)
                break;

            size_t chunk_len = (size_t)(obj_end - obj);
            char *chunk = malloc(chunk_len + 1);
            if (!chunk)
                break;
            memcpy(chunk, obj, chunk_len);
            chunk[chunk_len] = '\0';

            snap.entries[idx].turn = _extract_int_in(chunk, chunk + chunk_len, "turn");
            snap.entries[idx].age_s = _extract_long_in(chunk, chunk + chunk_len, "age_s");
            char *tag = _extract_str_in(chunk, chunk + chunk_len, "tag");
            if (tag) {
                nc_strlcpy(snap.entries[idx].tag, tag, sizeof(snap.entries[idx].tag));
                free(tag);
            }

            free(chunk);
            cursor = obj_end;
            idx++;
        }
        snap.count = idx;
    }

    checkpoint_discipline_restore_state(&snap);
}

static void _restore_tool_verify(const char *json) {
    const char *tv_end = NULL;
    const char *tv_section = _find_section(json, "tool_verify", &tv_end);
    if (!tv_section || !tv_end) {
        tool_verify_restore(NULL);
        return;
    }

    tool_verify_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.count = _extract_int_in(tv_section, tv_end, "count");
    snap.total_verified = _extract_int_in(tv_section, tv_end, "total_verified");
    snap.total_flagged = _extract_int_in(tv_section, tv_end, "total_flagged");
    snap.consecutive_empty = _extract_int_in(tv_section, tv_end, "consecutive_empty");

    const char *records_end = NULL;
    const char *records = _find_array_in(tv_section, tv_end, "records", &records_end);
    if (records && records_end) {
        const char *cursor = records + 1;
        int idx = 0;
        while (cursor && cursor < records_end && idx < NC_TV_HISTORY_MAX) {
            const char *obj = strchr(cursor, '{');
            if (!obj || obj >= records_end)
                break;
            const char *obj_end = _scan_balanced(obj, records_end, '{', '}');
            if (!obj_end)
                break;

            size_t chunk_len = (size_t)(obj_end - obj);
            char *chunk = malloc(chunk_len + 1);
            if (!chunk)
                break;
            memcpy(chunk, obj, chunk_len);
            chunk[chunk_len] = '\0';

            char *tool = _extract_str_in(chunk, chunk + chunk_len, "tool");
            if (tool) {
                nc_strlcpy(snap.records[idx].tool, tool, sizeof(snap.records[idx].tool));
                free(tool);
            }
            snap.records[idx].output_len = _extract_int_in(chunk, chunk + chunk_len, "output_len");
            snap.records[idx].flagged = _extract_bool_in(chunk, chunk + chunk_len, "flagged");
            snap.records[idx].age_s = _extract_long_in(chunk, chunk + chunk_len, "age_s");

            free(chunk);
            cursor = obj_end;
            idx++;
        }
        snap.count = idx;
    }

    tool_verify_restore(&snap);
}

static void _restore_doom_loop(const char *json) {
    const char *doom_end = NULL;
    const char *doom_section = _find_section(json, "doom_loop", &doom_end);
    if (!doom_section || !doom_end) {
        doom_loop_restore(NULL);
        return;
    }

    doom_loop_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.enabled = _extract_bool_in(doom_section, doom_end, "enabled");
    snap.count = _extract_int_in(doom_section, doom_end, "count");
    snap.fire_count = _extract_int_in(doom_section, doom_end, "fire_count");

    const char *entries_end = NULL;
    const char *entries = _find_array_in(doom_section, doom_end, "entries", &entries_end);
    if (entries && entries_end) {
        const char *cursor = entries + 1;
        int idx = 0;
        while (cursor && cursor < entries_end && idx < DOOM_LOOP_WINDOW) {
            const char *obj = strchr(cursor, '{');
            if (!obj || obj >= entries_end)
                break;
            const char *obj_end = _scan_balanced(obj, entries_end, '{', '}');
            if (!obj_end)
                break;

            size_t chunk_len = (size_t)(obj_end - obj);
            char *chunk = malloc(chunk_len + 1);
            if (!chunk)
                break;
            memcpy(chunk, obj, chunk_len);
            chunk[chunk_len] = '\0';

            snap.entries[idx].hash = (uint32_t)_extract_ulong_in(chunk, chunk + chunk_len, "hash");
            char *tool = _extract_str_in(chunk, chunk + chunk_len, "tool");
            if (tool) {
                nc_strlcpy(snap.entries[idx].tool_name, tool, sizeof(snap.entries[idx].tool_name));
                free(tool);
            }

            free(chunk);
            cursor = obj_end;
            idx++;
        }
        snap.count = idx;
    }

    doom_loop_restore(&snap);
}

static void _restore_shared_pad(pipe_orch_t *o, const char *json) {
    const char *pad_end = NULL;
    const char *pad_section = _find_section(json, "shared_pad", &pad_end);
    if (!pad_section || !pad_end)
        return;

    shared_pad_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.persistent = _extract_bool_in(pad_section, pad_end, "persistent");
    char *path = _extract_str_in(pad_section, pad_end, "path");
    if (path) {
        nc_strlcpy(snap.path, path, sizeof(snap.path));
        free(path);
    }
    char *run_token = _extract_str_in(pad_section, pad_end, "run_token");
    if (run_token) {
        nc_strlcpy(snap.run_token, run_token, sizeof(snap.run_token));
        free(run_token);
    }

    const char *lines_end = NULL;
    const char *lines = _find_array_in(pad_section, pad_end, "lines", &lines_end);
    if (lines && lines_end) {
        const char *cursor = lines + 1;
        int idx = 0;
        while (cursor && cursor < lines_end && idx < SPAD_MAX_LINES) {
            const char *obj = strchr(cursor, '{');
            if (!obj || obj >= lines_end)
                break;
            const char *obj_end = _scan_balanced(obj, lines_end, '{', '}');
            if (!obj_end)
                break;

            size_t chunk_len = (size_t)(obj_end - obj);
            char *chunk = malloc(chunk_len + 1);
            if (!chunk)
                break;
            memcpy(chunk, obj, chunk_len);
            chunk[chunk_len] = '\0';

            char *author = _extract_str_in(chunk, chunk + chunk_len, "author");
            char *text = _extract_str_in(chunk, chunk + chunk_len, "text");
            if (author)
                nc_strlcpy(snap.lines[idx].author, author, sizeof(snap.lines[idx].author));
            if (text)
                nc_strlcpy(snap.lines[idx].text, text, sizeof(snap.lines[idx].text));
            snap.lines[idx].age_s = _extract_long_in(chunk, chunk + chunk_len, "age_s");
            if (snap.lines[idx].age_s < 0)
                snap.lines[idx].age_s = 0;

            free(author);
            free(text);
            free(chunk);
            cursor = obj_end;
            idx++;
        }
        snap.count = idx;
    }

    shared_pad_restore(&o->pad, &snap);
}

static void _restore_shared_pad_from_disk(shared_pad_t *pad) {
    if (!pad || !pad->persistent || !pad->path[0])
        return;

    shared_pad_t disk;
    shared_pad_snapshot_t snap;
    shared_pad_init_persistent(&disk, pad->path);
    if (shared_pad_load(&disk) != 0) {
        shared_pad_free(&disk);
        return;
    }

    shared_pad_snapshot(&disk, &snap);
    shared_pad_free(&disk);
    (void)shared_pad_merge_snapshot(pad, &snap);
}

static void _restore_ledger_events(pipe_orch_t *o, const char *json) {
    const char *arr_end = NULL;
    const char *arr = _find_array(json, "ledger_events", &arr_end);
    if (!arr || !arr_end)
        return;

    bool persistent = o->ledger.persistent;
    char log_path[sizeof(o->ledger.log_path)];
    char snap_path[sizeof(o->ledger.snap_path)];
    char run_token[sizeof(o->ledger.current_run_token)];
    nc_strlcpy(log_path, o->ledger.log_path, sizeof(log_path));
    nc_strlcpy(snap_path, o->ledger.snap_path, sizeof(snap_path));
    nc_strlcpy(run_token, o->ledger.current_run_token, sizeof(run_token));

    memset(o->ledger.events, 0, sizeof(o->ledger.events));
    o->ledger.nevents = 0;
    o->ledger.next_seq = 1;
    o->ledger.persistent = persistent;
    nc_strlcpy(o->ledger.log_path, log_path, sizeof(o->ledger.log_path));
    nc_strlcpy(o->ledger.snap_path, snap_path, sizeof(o->ledger.snap_path));
    nc_strlcpy(o->ledger.current_run_token, run_token, sizeof(o->ledger.current_run_token));

    const char *cursor = arr + 1;
    while (cursor && cursor < arr_end && o->ledger.nevents < LEDGER_MAX_EVENTS) {
        const char *obj = strchr(cursor, '{');
        if (!obj || obj >= arr_end)
            break;
        const char *obj_end = _scan_balanced(obj, arr_end, '{', '}');
        if (!obj_end)
            break;

        size_t chunk_len = (size_t)(obj_end - obj);
        char *chunk = malloc(chunk_len + 1);
        if (!chunk)
            break;
        memcpy(chunk, obj, chunk_len);
        chunk[chunk_len] = '\0';

        ledger_event_t *ev = &o->ledger.events[o->ledger.nevents];
        memset(ev, 0, sizeof(*ev));
        ev->sequence = _extract_int_in(chunk, chunk + chunk_len, "seq");
        ev->timestamp = _extract_long_in(chunk, chunk + chunk_len, "ts");

        char *type = _extract_str_in(chunk, chunk + chunk_len, "type");
        char *node = _extract_str_in(chunk, chunk + chunk_len, "node");
        char *detail = _extract_str_in(chunk, chunk + chunk_len, "detail");
        char *output = _extract_str_in(chunk, chunk + chunk_len, "output");
        char *ev_run_token = _extract_str_in(chunk, chunk + chunk_len, "run_token");
        if (type)
            nc_strlcpy(ev->type, type, sizeof(ev->type));
        if (node)
            nc_strlcpy(ev->node_id, node, sizeof(ev->node_id));
        if (detail)
            nc_strlcpy(ev->detail, detail, sizeof(ev->detail));
        if (_find_key(chunk, chunk + chunk_len, "output")) {
            ev->has_output = true;
            if (output)
                nc_strlcpy(ev->output, output, sizeof(ev->output));
        }
        if (_find_key(chunk, chunk + chunk_len, "exit_code")) {
            ev->has_exit_code = true;
            ev->exit_code = _extract_int_in(chunk, chunk + chunk_len, "exit_code");
        }
        if (_find_key(chunk, chunk + chunk_len, "run_token")) {
            ev->has_run_token = true;
            if (ev_run_token)
                nc_strlcpy(ev->run_token, ev_run_token, sizeof(ev->run_token));
        }
        free(type);
        free(node);
        free(detail);
        free(output);
        free(ev_run_token);

        if (ev->sequence <= 0)
            ev->sequence = o->ledger.next_seq;
        if (ev->sequence >= o->ledger.next_seq)
            o->ledger.next_seq = ev->sequence + 1;

        o->ledger.nevents++;
        free(chunk);
        cursor = obj_end;
    }
}

static void _rp_strset_add(char (*dst)[PIPE_ID_MAX], int max_entries, int *count, const char *val) {
    if (!dst || !count || max_entries <= 0 || !val || !val[0])
        return;
    for (int i = 0; i < *count; i++) {
        if (strcmp(dst[i], val) == 0)
            return;
    }
    if (*count >= max_entries)
        return;
    nc_strlcpy(dst[*count], val, sizeof(dst[0]));
    (*count)++;
}

static void _rp_strset_remove(char (*dst)[PIPE_ID_MAX], int *count, const char *val) {
    if (!dst || !count || !val || !val[0])
        return;
    for (int i = 0; i < *count; i++) {
        if (strcmp(dst[i], val) != 0)
            continue;
        for (int j = i; j < *count - 1; j++)
            nc_strlcpy(dst[j], dst[j + 1], sizeof(dst[0]));
        dst[*count - 1][0] = '\0';
        (*count)--;
        return;
    }
}

static void _replay_ledger_event(pipe_orch_t *o, const ledger_event_t *ev) {
    if (!o || !o->pipeline || !ev)
        return;

    pipeline_t *p = o->pipeline;
    int idx = ev->node_id[0] ? pipeline_find_node(p, ev->node_id) : -1;

    if (strcmp(ev->type, "run_started") == 0) {
        o->state = ORCH_RUNNING;
        return;
    }

    if (strcmp(ev->type, "cancel_requested") == 0) {
        cscope_request_cancel(&o->run_scope);
        return;
    }

    if (strcmp(ev->type, "run_cancelled") == 0) {
        o->state = ORCH_CANCELLED;
        o->run_scope.state = CSCOPE_CANCELLED;
        for (int i = 0; i < p->nnodes; i++) {
            if (p->nodes[i].status == PIPE_PENDING || p->nodes[i].status == PIPE_QUEUED)
                pipeline_mark_cancelled(p, i);
        }
        return;
    }

    if (strcmp(ev->type, "run_completed") == 0) {
        if (ev->detail[0] && strcmp(ev->detail, "failed") == 0) {
            o->state = ORCH_FAILED;
            p->status = PIPE_FAILED;
        } else {
            o->state = ORCH_DONE;
            p->status = PIPE_DONE;
        }
        return;
    }

    if (strcmp(ev->type, "node_cancel_flagged") == 0) {
        _rp_strset_add(o->cancel_flags, PIPE_ORCH_MAX_CANCEL_FLAGS, &o->ncancel_flags, ev->node_id);
        return;
    }

    if (strcmp(ev->type, "node_rerun_queued") == 0) {
        _rp_strset_add(o->rerun_queue, PIPE_ORCH_MAX_RERUN_QUEUE, &o->nrerun_queued, ev->node_id);
        return;
    }

    if (strcmp(ev->type, "node_rerun_activated") == 0) {
        _rp_strset_remove(o->rerun_queue, &o->nrerun_queued, ev->node_id);
        if (idx >= 0) {
            p->nodes[idx].status = PIPE_PENDING;
            p->nodes[idx].finished_ms = 0;
        }
        return;
    }

    if (strcmp(ev->type, "node_cycle_restart") == 0) {
        if (ev->node_id[0])
            (void)rcycle_do_restart(&o->cycles, ev->node_id, p);
        return;
    }

    if (strcmp(ev->type, "fail_fast_skip") == 0) {
        if (idx >= 0)
            (void)pipeline_skip_downstream(p, idx);
        return;
    }

    if (idx < 0)
        return;

    if (strcmp(ev->type, "node_started") == 0) {
        pipeline_mark_running(p, idx);
        free(p->nodes[idx].output);
        p->nodes[idx].output = NULL;
        p->nodes[idx].exit_code = 0;
        return;
    }

    if (strcmp(ev->type, "node_done") == 0) {
        pipeline_mark_done(p, idx, ev->has_output ? ev->output : NULL,
                           ev->has_exit_code ? ev->exit_code : 0);
        _rp_strset_remove(o->cancel_flags, &o->ncancel_flags, ev->node_id);
        _rp_strset_remove(o->rerun_queue, &o->nrerun_queued, ev->node_id);
        return;
    }

    if (strcmp(ev->type, "node_failed") == 0) {
        pipeline_mark_failed(p, idx, ev->has_output ? ev->output : NULL,
                             ev->has_exit_code ? ev->exit_code : 1);
        _rp_strset_remove(o->cancel_flags, &o->ncancel_flags, ev->node_id);
        _rp_strset_remove(o->rerun_queue, &o->nrerun_queued, ev->node_id);
        return;
    }

    if (strcmp(ev->type, "node_cancelled") == 0) {
        pipeline_mark_cancelled(p, idx);
        _rp_strset_remove(o->cancel_flags, &o->ncancel_flags, ev->node_id);
        _rp_strset_remove(o->rerun_queue, &o->nrerun_queued, ev->node_id);
        return;
    }

    if (strcmp(ev->type, "node_skipped") == 0) {
        p->nodes[idx].status = PIPE_SKIPPED;
        p->nodes[idx].finished_ms = nc_mono_ms();
        _rp_strset_remove(o->cancel_flags, &o->ncancel_flags, ev->node_id);
        _rp_strset_remove(o->rerun_queue, &o->nrerun_queued, ev->node_id);
    }
}

static bool _ledger_event_matches_run(const pipe_orch_t *o, const ledger_event_t *ev,
                                      bool strict_missing_token) {
    if (!o || !ev || !o->run_scope.token[0])
        return true;
    if (!ev->has_run_token || !ev->run_token[0])
        return !strict_missing_token;
    return strcmp(o->run_scope.token, ev->run_token) == 0;
}

static bool _ledger_source_has_any_event_token(const event_ledger_t *ledger) {
    if (!ledger)
        return true;
    for (int i = 0; i < ledger->nevents; i++) {
        if (ledger->events[i].has_run_token && ledger->events[i].run_token[0])
            return true;
    }
    return false;
}

static void _replay_ledger_delta(pipe_orch_t *o, int from_seq) {
    if (!o || from_seq <= 0)
        return;
    for (int i = 0; i < o->ledger.nevents; i++) {
        const ledger_event_t *ev = &o->ledger.events[i];
        if (ev->sequence < from_seq)
            continue;
        if (!_ledger_event_matches_run(o, ev, false))
            continue;
        _replay_ledger_event(o, ev);
    }
}

static bool _ledger_has_run_identity(const event_ledger_t *ledger) {
    if (!ledger)
        return false;
    if (ledger->current_run_token[0])
        return true;
    for (int i = 0; i < ledger->nevents; i++) {
        if (ledger->events[i].has_run_token && ledger->events[i].run_token[0])
            return true;
    }
    return false;
}

static void _ledger_compact_events_for_run(const pipe_orch_t *o, event_ledger_t *ledger,
                                           bool strict_missing_token) {
    if (!o || !ledger)
        return;
    int keep = 0;
    for (int i = 0; i < ledger->nevents; i++) {
        if (!_ledger_event_matches_run(o, &ledger->events[i], strict_missing_token))
            continue;
        if (keep != i)
            ledger->events[keep] = ledger->events[i];
        keep++;
    }
    for (int i = keep; i < ledger->nevents; i++)
        memset(&ledger->events[i], 0, sizeof(ledger->events[i]));
    ledger->nevents = keep;
}

static void _restore_ledger_from_log(pipe_orch_t *o) {
    if (!o || !o->ledger.persistent || !o->ledger.log_path[0])
        return;

    event_ledger_t *ledger = &o->ledger;
    event_ledger_t disk;
    ledger_init_persistent(&disk, ledger->log_path, ledger->snap_path);
    int loaded = ledger_load_log(&disk);
    if (loaded <= 0)
        return;
    if (_ledger_has_run_identity(ledger) && !disk.current_run_token[0])
        return;
    if (o->run_scope.token[0] && disk.current_run_token[0] &&
        strcmp(o->run_scope.token, disk.current_run_token) != 0)
        return;
    bool strict_missing_token = _ledger_source_has_any_event_token(&disk);
    _ledger_compact_events_for_run(o, &disk, strict_missing_token);

    if (disk.next_seq > ledger->next_seq ||
        (disk.next_seq == ledger->next_seq && disk.nevents >= ledger->nevents)) {
        memcpy(ledger->events, disk.events, sizeof(ledger->events));
        ledger->nevents = disk.nevents;
        ledger->next_seq = disk.next_seq;
        if (disk.current_run_token[0])
            nc_strlcpy(ledger->current_run_token, disk.current_run_token,
                       sizeof(ledger->current_run_token));
    }
}

static void _normalize_restored_inflight_node(pipe_node_t *node) {
    if (!node || node->status != PIPE_RUNNING)
        return;

    /*
     * A restored snapshot has no live worker behind an in-flight node.
     * Re-queue it as pending so the scheduler can launch it again instead
     * of leaving a phantom running slot that blocks future work.
     */
    node->status = PIPE_PENDING;
    node->exit_code = 0;
    node->started_ms = 0;
    node->finished_ms = 0;
    free(node->output);
    node->output = NULL;
}

int run_persist_restore_json(pipe_orch_t *o, pipeline_t *p, const char *json) {
    if (!o || !p || !json)
        return -1;

    char *magic = _extract_str(json, "magic");
    if (!magic || strcmp(magic, RUN_PERSIST_MAGIC) != 0) {
        free(magic);
        return -1;
    }
    free(magic);

    int ver = _extract_int(json, "version");
    if (ver < 1 || ver > RUN_PERSIST_VERSION)
        return -1;

    const char *pipe_end = NULL;
    const char *pipe_section = _find_section(json, "pipeline", &pipe_end);
    if (pipe_section && pipe_end) {
        char *pname = _extract_str_in(pipe_section, pipe_end, "name");
        if (pname) {
            nc_strlcpy(p->name, pname, sizeof(p->name));
            free(pname);
        }
        p->concurrency = _extract_int_in(pipe_section, pipe_end, "concurrency");
        if (p->concurrency <= 0)
            p->concurrency = PIPE_MAX_CONCURRENT;
        p->max_iterations = _extract_int_in(pipe_section, pipe_end, "max_iterations");
        if (p->max_iterations <= 0)
            p->max_iterations = RCYCLE_DEFAULT_MAX_ITERS;
        p->fail_fast = (strstr(pipe_section, "\"fail_fast\": true") != NULL ||
                        strstr(pipe_section, "\"fail_fast\":true") != NULL);
        char *pstatus = _extract_str_in(pipe_section, pipe_end, "status");
        if (pstatus) {
            p->status = _parse_status(pstatus);
            free(pstatus);
        }
        p->started_ms = _extract_long_in(pipe_section, pipe_end, "started_ms");
        p->finished_ms = _extract_long_in(pipe_section, pipe_end, "finished_ms");
    }

    const char *orch_end = NULL;
    const char *orch_section = _find_section(json, "orchestrator", &orch_end);
    if (orch_section && orch_end) {
        o->state = (orch_state_t)_extract_int_in(orch_section, orch_end, "state");
        o->started_ms = _extract_long_in(orch_section, orch_end, "started_ms");
        o->finished_ms = _extract_long_in(orch_section, orch_end, "finished_ms");
        o->ctx_rendering = _extract_bool_in(orch_section, orch_end, "ctx_rendering");
        int run_scope_state = _extract_int_in(orch_section, orch_end, "run_scope_state");
        if (run_scope_state >= CSCOPE_ACTIVE && run_scope_state <= CSCOPE_CANCELLED)
            o->run_scope.state = (cscope_state_t)run_scope_state;
        else
            o->run_scope.state = CSCOPE_ACTIVE;
        long run_scope_age_ms =
            _extract_long_in(orch_section, orch_end, "run_scope_requested_age_ms");
        if (run_scope_age_ms < 0)
            run_scope_age_ms = 0;
        if (o->run_scope.state == CSCOPE_ACTIVE) {
            o->run_scope.requested_ms = 0;
        } else {
            o->run_scope.requested_ms = nc_mono_ms() - run_scope_age_ms;
            if (o->run_scope.requested_ms < 0)
                o->run_scope.requested_ms = 0;
        }
        o->run_scope.parent = NULL;
        o->run_scope.depth = 0;
        char *run_scope_token = _extract_str_in(orch_section, orch_end, "run_scope_token");
        if (run_scope_token && run_scope_token[0])
            nc_strlcpy(o->run_scope.token, run_scope_token, sizeof(o->run_scope.token));
        else
            nc_strlcpy(o->run_scope.token, p->name, sizeof(o->run_scope.token));
        free(run_scope_token);
        ledger_set_run_token(&o->ledger, o->run_scope.token);
        shared_pad_set_run_token(&o->pad, o->run_scope.token);
    }

    const char *nodes_end = NULL;
    const char *nodes = _find_array(json, "nodes", &nodes_end);
    if (nodes && nodes_end) {
        const char *cursor = nodes + 1;
        while (cursor && cursor < nodes_end) {
            const char *obj = strchr(cursor, '{');
            if (!obj || obj >= nodes_end)
                break;
            const char *obj_end = _scan_balanced(obj, nodes_end, '{', '}');
            if (!obj_end)
                break;

            size_t chunk_len = (size_t)(obj_end - obj);
            char *chunk = malloc(chunk_len + 1);
            if (!chunk)
                break;
            memcpy(chunk, obj, chunk_len);
            chunk[chunk_len] = '\0';

            char *nid = _extract_str_in(chunk, chunk + chunk_len, "id");
            char *nst = _extract_str_in(chunk, chunk + chunk_len, "status");

            if (nid) {
                int idx = pipeline_find_node(p, nid);
                if (idx >= 0) {
                    p->nodes[idx].status = _parse_status(nst);
                    p->nodes[idx].exit_code =
                        _extract_int_in(chunk, chunk + chunk_len, "exit_code");
                    p->nodes[idx].attempt = _extract_int_in(chunk, chunk + chunk_len, "attempt");
                    p->nodes[idx].retries = _extract_int_in(chunk, chunk + chunk_len, "retries");
                    p->nodes[idx].started_ms =
                        _extract_long_in(chunk, chunk + chunk_len, "started_ms");
                    p->nodes[idx].finished_ms =
                        _extract_long_in(chunk, chunk + chunk_len, "finished_ms");
                    if (_find_key(chunk, chunk + chunk_len, "output")) {
                        char *output = _extract_str_in(chunk, chunk + chunk_len, "output");
                        free(p->nodes[idx].output);
                        p->nodes[idx].output = output;
                    }
                    _normalize_restored_inflight_node(&p->nodes[idx]);
                }
            }
            free(nid);
            free(nst);
            free(chunk);
            cursor = obj_end;
        }
    }

    _restore_cycle_edges(o, json, ver);
    _restore_criteria(o, json);
    _restore_probes(o, json);
    _restore_watchdog(json);
    _restore_failure_router(json);
    _restore_stall_detector(json);
    _restore_context_fatigue(json);
    _restore_output_eval(json);
    _restore_auto_test(json);
    _restore_checkpoint_discipline(json);
    _restore_tool_verify(json);
    _restore_doom_loop(json);
    _restore_shared_pad(o, json);
    _restore_shared_pad_from_disk(&o->pad);
    _restore_ledger_events(o, json);
    bool strict_missing_token = _ledger_source_has_any_event_token(&o->ledger);
    int pre_compact_count = o->ledger.nevents;
    _ledger_compact_events_for_run(o, &o->ledger, strict_missing_token);
    if (o->ledger.nevents < pre_compact_count) {
        bool has_run_complete = false;
        for (int i = 0; i < o->ledger.nevents; i++) {
            if (strcmp(o->ledger.events[i].type, "run_completed") == 0) {
                has_run_complete = true;
                break;
            }
        }
        if (!has_run_complete && (o->state == ORCH_DONE || o->state == ORCH_FAILED)) {
            o->state = ORCH_RUNNING;
            if (p->status == PIPE_DONE || p->status == PIPE_FAILED)
                p->status = PIPE_PENDING;
        }
        for (int ni = 0; ni < p->nnodes; ni++) {
            pipe_status_t ns = p->nodes[ni].status;
            if (ns != PIPE_DONE && ns != PIPE_FAILED && ns != PIPE_SKIPPED && ns != PIPE_CANCELLED)
                continue;
            bool has_terminal_ev = false;
            for (int ei = 0; ei < o->ledger.nevents; ei++) {
                const ledger_event_t *ev = &o->ledger.events[ei];
                if (strcmp(ev->node_id, p->nodes[ni].id) != 0)
                    continue;
                if (strcmp(ev->type, "node_done") == 0 || strcmp(ev->type, "node_failed") == 0 ||
                    strcmp(ev->type, "node_skipped") == 0 ||
                    strcmp(ev->type, "node_cancelled") == 0) {
                    has_terminal_ev = true;
                    break;
                }
            }
            if (!has_terminal_ev) {
                bool was_started = false;
                for (int ei = 0; ei < o->ledger.nevents; ei++) {
                    if (strcmp(o->ledger.events[ei].node_id, p->nodes[ni].id) == 0 &&
                        strcmp(o->ledger.events[ei].type, "node_started") == 0) {
                        was_started = true;
                        break;
                    }
                }
                p->nodes[ni].status = was_started ? PIPE_PENDING : PIPE_PENDING;
                free(p->nodes[ni].output);
                p->nodes[ni].output = NULL;
                p->nodes[ni].exit_code = 0;
                p->nodes[ni].finished_ms = 0;
            }
        }
    }
    int snapshot_ledger_next_seq = o->ledger.next_seq;

    _restore_string_array(o->cancel_flags, PIPE_ORCH_MAX_CANCEL_FLAGS, sizeof(o->cancel_flags[0]),
                          &o->ncancel_flags, json, "cancel_flags");
    _restore_string_array(o->rerun_queue, PIPE_ORCH_MAX_RERUN_QUEUE, sizeof(o->rerun_queue[0]),
                          &o->nrerun_queued, json, "rerun_queue");
    if (o->artifacts)
        (void)artifact_store_rebuild(o->artifacts);

    _restore_ledger_from_log(o);
    _replay_ledger_delta(o, snapshot_ledger_next_seq);
    for (int i = 0; i < p->nnodes; i++)
        _normalize_restored_inflight_node(&p->nodes[i]);

    return 0;
}

int run_persist_load(pipe_orch_t *o, pipeline_t *p, const char *path) {
    if (!o || !p || !path)
        return -1;

    char *json = _load_preferred_snapshot_json(o, path);
    if (!json)
        return -1;

    int rc = run_persist_restore_json(o, p, json);
    free(json);

    return rc;
}
