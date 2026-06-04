/*
 * hypothesis_debug.c — Hypothesis-driven debugging framework.
 *
 * When the agent detects a stall or bug, it opens a debug session,
 * proposes hypotheses with confidence scores, runs tests to confirm
 * or refute them, and injects structured debugging context into the
 * conversation so the model can reason systematically.
 */

#include "nevercode.h"
#include "defaults.h"

typedef enum {
    HYP_PROPOSED = 0,
    HYP_TESTING,
    HYP_CONFIRMED,
    HYP_REFUTED,
    HYP_INCONCLUSIVE
} hyp_status_t;

typedef struct {
    char description[HYP_DESC_MAX];
    char evidence[HYP_EVIDENCE_MAX];
    hyp_status_t status;
    int tests_run;
    double confidence;
    long created_at;
    long resolved_at;
} hypothesis_t;

typedef struct {
    char symptom[HYP_DESC_MAX];
    hypothesis_t hyps[HYP_MAX_PER_SESSION];
    int count;
    int confirmed;
    int refuted;
    int total_test_steps;
    int session_runs;
    bool active;
} hyp_session_t;

static struct {
    hyp_session_t sessions[HYP_MAX_SESSIONS];
    int count;
    int total_sessions;
    int total_hypotheses;
    bool init;
} g_hd;

/* ── Init / Free ── */

void hypothesis_debug_init(void) {
    memset(&g_hd, 0, sizeof(g_hd));
    g_hd.init = true;
}

void hypothesis_debug_free(void) {
    memset(&g_hd, 0, sizeof(g_hd));
}

/* ── Session lifecycle ── */

int hypothesis_debug_begin(const char *symptom) {
    if (!g_hd.init || !symptom || g_hd.count >= HYP_MAX_SESSIONS)
        return -1;

    hyp_session_t *s = &g_hd.sessions[g_hd.count];
    memset(s, 0, sizeof(*s));
    nc_strlcpy(s->symptom, symptom, sizeof(s->symptom));
    s->active = true;
    s->session_runs = 1;
    g_hd.count++;
    g_hd.total_sessions++;
    return g_hd.count - 1;
}

int hypothesis_debug_propose(int session, const char *desc, double conf) {
    if (!g_hd.init || session < 0 || session >= g_hd.count)
        return -1;

    hyp_session_t *s = &g_hd.sessions[session];
    if (s->count >= HYP_MAX_PER_SESSION)
        return -1;

    hypothesis_t *h = &s->hyps[s->count];
    memset(h, 0, sizeof(*h));
    nc_strlcpy(h->description, desc ? desc : "", sizeof(h->description));
    h->status = HYP_PROPOSED;
    h->confidence = conf > 0.0 ? conf : 0.5;
    h->created_at = nc_wall_sec();
    s->count++;
    g_hd.total_hypotheses++;
    return s->count - 1;
}

int hypothesis_debug_test(int session, int hyp_idx) {
    if (!g_hd.init || session < 0 || session >= g_hd.count)
        return -1;
    hyp_session_t *s = &g_hd.sessions[session];
    if (hyp_idx < 0 || hyp_idx >= s->count)
        return -1;

    s->hyps[hyp_idx].status = HYP_TESTING;
    s->hyps[hyp_idx].tests_run++;
    s->total_test_steps++;
    return 0;
}

int hypothesis_debug_confirm(int session, int hyp_idx, const char *evidence) {
    if (!g_hd.init || session < 0 || session >= g_hd.count)
        return -1;
    hyp_session_t *s = &g_hd.sessions[session];
    if (hyp_idx < 0 || hyp_idx >= s->count)
        return -1;

    hypothesis_t *h = &s->hyps[hyp_idx];
    h->status = HYP_CONFIRMED;
    h->confidence = 1.0;
    h->resolved_at = nc_wall_sec();
    if (evidence)
        nc_strlcpy(h->evidence, evidence, sizeof(h->evidence));
    s->confirmed++;
    return 0;
}

int hypothesis_debug_refute(int session, int hyp_idx, const char *evidence) {
    if (!g_hd.init || session < 0 || session >= g_hd.count)
        return -1;
    hyp_session_t *s = &g_hd.sessions[session];
    if (hyp_idx < 0 || hyp_idx >= s->count)
        return -1;

    hypothesis_t *h = &s->hyps[hyp_idx];
    h->status = HYP_REFUTED;
    h->confidence = 0.0;
    h->resolved_at = nc_wall_sec();
    if (evidence)
        nc_strlcpy(h->evidence, evidence, sizeof(h->evidence));
    s->refuted++;
    return 0;
}

int hypothesis_debug_update_confidence(int session, int hyp_idx, double c) {
    if (!g_hd.init || session < 0 || session >= g_hd.count)
        return -1;
    hyp_session_t *s = &g_hd.sessions[session];
    if (hyp_idx < 0 || hyp_idx >= s->count)
        return -1;

    if (c < 0.0)
        c = 0.0;
    if (c > 1.0)
        c = 1.0;
    s->hyps[hyp_idx].confidence = c;
    return 0;
}

int hypothesis_debug_end(int session) {
    if (!g_hd.init || session < 0 || session >= g_hd.count)
        return -1;
    g_hd.sessions[session].active = false;
    return 0;
}

/* ── Auto-populate: bridge stall detection → hypothesis generation ── */

static const char *LOOP_HYPOTHESES[] = {
    "Tool output empty or too short — agent retries assuming incomplete",
    "Tool result not written back to context correctly",
    "Goal lacks a progress marker or completion signal",
    "Context compaction dropped recent execution results",
    "Tool router returns success but the operation semantically failed",
    "Identical parameters re-submitted due to stale planner state",
};
#define LOOP_HYP_COUNT NC_ARRAY_LEN(LOOP_HYPOTHESES)

static const char *NOPROG_HYPOTHESES[] = {
    "Working on the wrong sub-task — goal misaligned with current action",
    "Blocked by an error that is silently swallowed",
    "Read-only exploration loop without any write/modify action",
    "Missing prerequisite: a dependency or file not yet created",
    "Context window nearly full — model output is being truncated",
    "Recovery strategy from previous stall did not take effect",
};
#define NOPROG_HYP_COUNT NC_ARRAY_LEN(NOPROG_HYPOTHESES)

static const char *TIMEOUT_HYPOTHESES[] = {
    "Long-running subprocess hanging without timeout",
    "Network request stalled — API endpoint unresponsive",
    "Infinite retry loop in failure router",
    "Blocking I/O without progress callback",
};
#define TIMEOUT_HYP_COUNT NC_ARRAY_LEN(TIMEOUT_HYPOTHESES)

int hypothesis_debug_auto_populate(int session, int stall_type) {
    if (!g_hd.init || session < 0 || session >= g_hd.count)
        return -1;

    const char **hyps;
    int count;

    switch (stall_type) {
    case 1: /* STALL_LOOP_DETECTED */
        hyps = LOOP_HYPOTHESES;
        count = LOOP_HYP_COUNT;
        break;
    case 2: /* STALL_NO_PROGRESS */
        hyps = NOPROG_HYPOTHESES;
        count = NOPROG_HYP_COUNT;
        break;
    case 3: /* STALL_TIMEOUT */
        hyps = TIMEOUT_HYPOTHESES;
        count = TIMEOUT_HYP_COUNT;
        break;
    default:
        hyps = LOOP_HYPOTHESES;
        count = LOOP_HYP_COUNT;
        break;
    }

    int added = 0;
    for (int i = 0; i < count; i++) {
        double conf = 0.7 - (i * 0.08);
        if (conf < 0.2)
            conf = 0.2;
        if (hypothesis_debug_propose(session, hyps[i], conf) >= 0)
            added++;
    }
    return added;
}

/* ── Queries ── */

int hypothesis_debug_session_count(void) {
    return g_hd.count;
}
int hypothesis_debug_total_hypotheses(void) {
    return g_hd.total_hypotheses;
}

int hypothesis_debug_hyp_count(int s) {
    if (s < 0 || s >= g_hd.count)
        return 0;
    return g_hd.sessions[s].count;
}

int hypothesis_debug_confirmed_count(int s) {
    if (s < 0 || s >= g_hd.count)
        return 0;
    return g_hd.sessions[s].confirmed;
}

int hypothesis_debug_refuted_count(int s) {
    if (s < 0 || s >= g_hd.count)
        return 0;
    return g_hd.sessions[s].refuted;
}

/* ── Status name ── */

static const char *hyp_status_name(hyp_status_t st) {
    switch (st) {
    case HYP_PROPOSED:
        return "proposed";
    case HYP_TESTING:
        return "testing";
    case HYP_CONFIRMED:
        return "confirmed";
    case HYP_REFUTED:
        return "refuted";
    case HYP_INCONCLUSIVE:
        return "inconclusive";
    }
    return "unknown";
}

/* ── Context building ── */

char *hypothesis_debug_build_prompt(int session) {
    if (!g_hd.init || session < 0 || session >= g_hd.count)
        return nc_xstrdup("");

    hyp_session_t *s = &g_hd.sessions[session];
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb,
                   "<debugging_context>\n"
                   "Symptom: %s\n"
                   "Hypotheses (%d total, %d confirmed, %d refuted):\n",
                   s->symptom, s->count, s->confirmed, s->refuted);

    for (int i = 0; i < s->count; i++) {
        hypothesis_t *h = &s->hyps[i];
        strbuf_appendf(&sb, "  [%d] (%s, confidence=%.0f%%) %s\n", i, hyp_status_name(h->status),
                       h->confidence * 100.0, h->description);
        if (h->evidence[0])
            strbuf_appendf(&sb, "       evidence: %s\n", h->evidence);
    }

    int unresolved = s->count - s->confirmed - s->refuted;
    if (unresolved > 0)
        strbuf_appendf(&sb, "Action: Test the highest-confidence unresolved hypothesis next.\n");
    else if (s->confirmed == 0 && s->count > 0)
        strbuf_appendf(&sb, "All hypotheses refuted. Propose new hypotheses.\n");

    strbuf_appendf(&sb, "</debugging_context>\n");
    return strbuf_detach(&sb);
}

/* ── Formatting ── */

char *hypothesis_debug_format(int session) {
    if (!g_hd.init || session < 0 || session >= g_hd.count)
        return nc_xstrdup("(no session)");

    hyp_session_t *s = &g_hd.sessions[session];
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb,
                   "Debug Session %d: %s (%s)\n"
                   "  hypotheses=%d confirmed=%d refuted=%d tests=%d\n",
                   session, s->symptom, s->active ? "active" : "closed", s->count, s->confirmed,
                   s->refuted, s->total_test_steps);

    for (int i = 0; i < s->count; i++) {
        hypothesis_t *h = &s->hyps[i];
        strbuf_appendf(&sb, "  [%d] %s (%.0f%%) %s\n", i, hyp_status_name(h->status),
                       h->confidence * 100.0, h->description);
    }
    return strbuf_detach(&sb);
}

char *hypothesis_debug_format_all(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Hypothesis Debug: %d sessions, %d total hypotheses\n", g_hd.count,
                   g_hd.total_hypotheses);

    for (int i = 0; i < g_hd.count; i++) {
        char *s = hypothesis_debug_format(i);
        strbuf_append_str(&sb, s);
        free(s);
    }
    return strbuf_detach(&sb);
}
