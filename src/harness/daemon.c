/*
 * daemon.c — Long-running agent daemon mode
 *
 * Manages daemon lifecycle, session statistics, and the harness
 * controller that ties failure routing, goal tracking, and context
 * handoffs together.
 */

#include "harness.h"
#include "conversation.h"

/* ── Daemon Context ── */

void daemon_ctx_init(daemon_ctx_t *d) {
    memset(d, 0, sizeof(*d));
    d->state = DAEMON_IDLE;
    d->start_time = nc_wall_sec();
    d->last_activity = d->start_time;
    d->auto_evolve = true;
    goal_tracker_init(&d->goals);
}

void daemon_ctx_free(daemon_ctx_t *d) {
    goal_tracker_free(&d->goals);
    free(d->fail.last_msg);
    memset(d, 0, sizeof(*d));
}

bool daemon_is_running(const daemon_ctx_t *d) {
    return d->state == DAEMON_RUNNING;
}

void daemon_record_turn(daemon_ctx_t *d) {
    d->total_turns++;
    d->last_activity = nc_wall_sec();
    d->state = DAEMON_RUNNING;
    d->fail.consecutive = 0;
}

void daemon_record_failure(daemon_ctx_t *d, failure_class_t cls, const char *msg) {
    d->total_failures++;
    d->fail.cls = cls;
    d->fail.consecutive++;
    d->fail.last_ms = nc_wall_sec() * 1000;
    free(d->fail.last_msg);
    d->fail.last_msg = msg ? nc_xstrdup(msg) : NULL;
}

void daemon_record_handoff(daemon_ctx_t *d) {
    d->total_handoffs++;
    d->last_activity = nc_wall_sec();
}

long daemon_uptime_s(const daemon_ctx_t *d) {
    return nc_wall_sec() - d->start_time;
}

char *daemon_format_status(const daemon_ctx_t *d) {
    static const char *state_names[] = {"idle", "running", "paused", "shutdown"};

    strbuf_t sb;
    strbuf_init(&sb);

    long uptime = daemon_uptime_s(d);
    char uptime_s[NC_FMT_MAX];
    nc_fmt_elapsed_s(uptime_s, sizeof(uptime_s), (int)uptime);

    strbuf_appendf(&sb, "Daemon: %s | uptime: %s | turns: %d | handoffs: %d | failures: %d",
                   state_names[d->state], uptime_s, d->total_turns, d->total_handoffs,
                   d->total_failures);

    if (d->fail.consecutive > 0)
        strbuf_appendf(&sb, " | consecutive_failures: %d (%s)", d->fail.consecutive,
                       harness_failure_name(d->fail.cls));

    strbuf_append_str(&sb, "\n");

    if (d->goals.count > 0) {
        char *goals = goal_tracker_format_status(&d->goals);
        if (goals) {
            strbuf_append_str(&sb, goals);
            free(goals);
        }
    }

    return strbuf_detach(&sb);
}

/* ── Harness Controller ── */

static harness_ctx_t g_harness;

void harness_init(harness_ctx_t *h) {
    memset(h, 0, sizeof(*h));
    daemon_ctx_init(&h->daemon);
    handoff_state_init(&h->handoff);
}

void harness_free(harness_ctx_t *h) {
    daemon_ctx_free(&h->daemon);
    handoff_state_free(&h->handoff);
    memset(h, 0, sizeof(*h));
}

void harness_post_turn(harness_ctx_t *h, int context_pct, int tool_calls, bool had_error) {
    (void)tool_calls;
    h->context_pct = context_pct;

    if (had_error)
        h->daemon.total_failures++;
    else
        daemon_record_turn(&h->daemon);

    h->needs_handoff = (context_pct >= HARNESS_CONTEXT_EXHAUSTION_PCT);
}

bool harness_needs_handoff(const harness_ctx_t *h) {
    return h->needs_handoff && h->handoff_retries < HARNESS_MAX_HANDOFF_RETRIES;
}

static void extract_conv_state(handoff_state_t *hs, const conversation_t *c) {
    if (!c || c->len == 0)
        return;

    strbuf_t goals, decisions, pending, files, findings;
    strbuf_init(&goals);
    strbuf_init(&decisions);
    strbuf_init(&pending);
    strbuf_init(&files);
    strbuf_init(&findings);

    int scan_start = c->len > 20 ? c->len - 20 : 0;

    for (int i = scan_start; i < c->len; i++) {
        const message_t *m = &c->msgs[i];
        for (int j = 0; j < m->num_blocks; j++) {
            const content_block_t *b = &m->blocks[j];

            if (b->type == CONTENT_TEXT && b->text) {
                if (m->role == ROLE_USER && i >= c->len - 4) {
                    if (goals.len == 0)
                        strbuf_append_str(&goals, b->text);
                    else {
                        strbuf_append_str(&pending, "- ");
                        size_t tl = strlen(b->text);
                        if (tl > 200) {
                            strbuf_append(&pending, b->text, 200);
                            strbuf_append_str(&pending, "...");
                        } else {
                            strbuf_append_str(&pending, b->text);
                        }
                        strbuf_append_str(&pending, "\n");
                    }
                }
                if (m->role == ROLE_ASSISTANT && i >= c->len - 6) {
                    size_t tl = strlen(b->text);
                    size_t cap = tl > NC_PREVIEW_HALF ? NC_PREVIEW_HALF : tl;
                    if (findings.len < NC_DAEMON_FINDINGS_CAP) {
                        strbuf_append(&findings, b->text, cap);
                        if (cap < tl)
                            strbuf_append_str(&findings, "...");
                        strbuf_append_str(&findings, "\n");
                    }
                }
            }

            if (b->type == CONTENT_TOOL_USE && b->tool_name) {
                if (files.len < NC_DAEMON_FILES_CAP) {
                    strbuf_appendf(&files, "- %s", b->tool_name);
                    if (b->tool_input) {
                        cJSON *path = cJSON_GetObjectItem(b->tool_input, "file_path");
                        if (!path)
                            path = cJSON_GetObjectItem(b->tool_input, "path");
                        if (path && cJSON_IsString(path))
                            strbuf_appendf(&files, " (%s)", path->valuestring);
                    }
                    strbuf_append_str(&files, "\n");
                }
            }
        }
    }

    free(hs->goal_summary);
    hs->goal_summary = goals.len > 0 ? strbuf_detach(&goals) : NULL;
    strbuf_free(&goals);

    free(hs->decisions_made);
    hs->decisions_made = decisions.len > 0 ? strbuf_detach(&decisions) : NULL;
    strbuf_free(&decisions);

    free(hs->pending_tasks);
    hs->pending_tasks = pending.len > 0 ? strbuf_detach(&pending) : NULL;
    strbuf_free(&pending);

    free(hs->files_modified);
    hs->files_modified = files.len > 0 ? strbuf_detach(&files) : NULL;
    strbuf_free(&files);

    free(hs->key_findings);
    hs->key_findings = findings.len > 0 ? strbuf_detach(&findings) : NULL;
    strbuf_free(&findings);
}

char *harness_do_handoff(harness_ctx_t *h, const void *conv, const char *session_id) {
    h->handoff.handoff_count++;
    h->handoff.elapsed_s = daemon_uptime_s(&h->daemon);
    h->handoff.turn_count = h->daemon.total_turns;
    h->handoff_retries++;

    if (conv)
        extract_conv_state(&h->handoff, (const conversation_t *)conv);

    daemon_record_handoff(&h->daemon);
    harness_checkpoint_save(session_id, h->daemon.total_turns, &h->handoff);

    char *resume = handoff_build_resume_prompt(&h->handoff);
    h->needs_handoff = false;

    return resume;
}

harness_ctx_t *harness_global(void) {
    return &g_harness;
}
