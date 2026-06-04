/*
 * tool_verify.c — Post-execution verification scanner for tool calls.
 *
 * Detects silent tool failures (the costliest failure mode in autonomous
 * agents).  Classifies each result as pass / empty / suspicious /
 * explicit-fail / pattern-alert.  Three consecutive empty outputs from
 * non-write tools trigger a pattern alert requiring intervention.
 */

#include "harness.h"

/* NC_NC_TV_HISTORY_MAX in defaults.h */

typedef struct {
    char tool[NC_TAG_MAX];
    int output_len;
    bool flagged;
    long timestamp;
} tv_record_t;

typedef struct {
    tv_record_t records[NC_TV_HISTORY_MAX];
    int count;
    int total_verified;
    int total_flagged;
    int consecutive_empty;
} tv_ctx_t;

static tv_ctx_t g_tv;

/* ── Init / Free ── */

void tool_verify_init(void) {
    memset(&g_tv, 0, sizeof(g_tv));
}

void tool_verify_free(void) {
    memset(&g_tv, 0, sizeof(g_tv));
}

/* ── Tool classification helpers ── */

static bool is_write_tool(const char *name) {
    if (!name)
        return false;
    return strstr(name, "write") || strstr(name, "edit") || strstr(name, "patch") ||
           strstr(name, "create") || strstr(name, "mkdir");
}

static bool is_read_tool(const char *name) {
    if (!name)
        return false;
    return strstr(name, "read") || strstr(name, "glob") || strstr(name, "grep") ||
           strstr(name, "list") || strstr(name, "search") || strstr(name, "diff") ||
           strstr(name, "fetch");
}

/* Tools where non-zero exit / is_error is a normal business outcome
 * (e.g. file not found, URL unreachable) — not worth flagging. */
static bool error_is_normal(const char *name) {
    if (!name)
        return false;
    if (strcmp(name, "Batch") == 0 || strcmp(name, "Bash") == 0)
        return true;
    return strstr(name, "read") || strstr(name, "fetch") || strstr(name, "search") ||
           strstr(name, "glob") || strstr(name, "grep") || strstr(name, "list") ||
           strstr(name, "diff");
}

static bool has_silent_error_pattern(const char *output, int output_len) {
    if (!output || output_len < 5)
        return false;
    static const char *patterns[] = {"permission denied",
                                     "no such file",
                                     "command not found",
                                     "connection refused",
                                     "connection timed out",
                                     "segmentation fault",
                                     "killed",
                                     "core dumped",
                                     "out of memory",
                                     "disk quota exceeded",
                                     "broken pipe",
                                     NULL};

    int scan_len = output_len > TV_SCAN_HEAD_BYTES ? TV_SCAN_HEAD_BYTES : output_len;
    for (int i = 0; patterns[i]; i++) {
        int plen = (int)strlen(patterns[i]);
        for (int j = 0; j <= scan_len - plen; j++) {
            bool match = true;
            for (int k = 0; k < plen && match; k++) {
                char oc = output[j + k];
                char pc = patterns[i][k];
                if (oc >= 'A' && oc <= 'Z')
                    oc += 32;
                if (oc != pc)
                    match = false;
            }
            if (match)
                return true;
        }
    }
    return false;
}

/* ── Verify a single tool call result ── */

tv_verdict_t tool_verify_check(const char *tool_name, const char *output, int output_len,
                               int exit_code, bool is_error) {
    tv_verdict_t v = TV_PASS;

    if (is_error || exit_code != 0) {
        v = error_is_normal(tool_name) ? TV_PASS : TV_EXPLICIT_FAIL;
    } else if (!output || output_len == 0) {
        if (is_write_tool(tool_name))
            v = TV_PASS;
        else
            v = TV_EMPTY_OUTPUT;
        g_tv.consecutive_empty++;
    } else {
        g_tv.consecutive_empty = 0;
        if (is_read_tool(tool_name) && output_len < 3)
            v = TV_SUSPICIOUS;
        else if (!is_write_tool(tool_name) && has_silent_error_pattern(output, output_len))
            v = TV_SUSPICIOUS;
    }

    if (g_tv.consecutive_empty >= 3 && v == TV_EMPTY_OUTPUT)
        v = TV_PATTERN_ALERT;

    /* Maintain sliding history window */
    if (g_tv.count >= NC_TV_HISTORY_MAX) {
        memmove(&g_tv.records[0], &g_tv.records[1], (NC_TV_HISTORY_MAX - 1) * sizeof(tv_record_t));
        g_tv.count = NC_TV_HISTORY_MAX - 1;
    }

    tv_record_t *r = &g_tv.records[g_tv.count++];
    memset(r, 0, sizeof(*r));
    if (tool_name) {
        nc_strlcpy(r->tool, tool_name, sizeof(r->tool));
    }
    r->output_len = output_len;
    r->flagged = (v != TV_PASS);
    r->timestamp = nc_wall_sec();

    g_tv.total_verified++;
    if (v != TV_PASS)
        g_tv.total_flagged++;
    return v;
}

/* ── Verdict helpers ── */

const char *tv_verdict_name(tv_verdict_t v) {
    switch (v) {
    case TV_PASS:
        return "pass";
    case TV_EMPTY_OUTPUT:
        return "empty_output";
    case TV_SUSPICIOUS:
        return "suspicious";
    case TV_EXPLICIT_FAIL:
        return "explicit_fail";
    case TV_PATTERN_ALERT:
        return "pattern_alert";
    }
    return "unknown";
}

bool tool_verify_needs_intervention(tv_verdict_t v) {
    return v == TV_PATTERN_ALERT || v == TV_EXPLICIT_FAIL;
}

/* ── Stats ── */

int tool_verify_flagged_count(void) {
    return g_tv.total_flagged;
}
int tool_verify_total_count(void) {
    return g_tv.total_verified;
}

char *tool_verify_format_stats(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Verified: %d | Flagged: %d | Consecutive empty: %d", g_tv.total_verified,
                   g_tv.total_flagged, g_tv.consecutive_empty);
    return strbuf_detach(&sb);
}

void tool_verify_snapshot(tool_verify_snapshot_t *out) {
    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    long now_s = nc_wall_sec();
    out->count = g_tv.count;
    out->total_verified = g_tv.total_verified;
    out->total_flagged = g_tv.total_flagged;
    out->consecutive_empty = g_tv.consecutive_empty;

    if (out->count < 0)
        out->count = 0;
    if (out->count > NC_TV_HISTORY_MAX)
        out->count = NC_TV_HISTORY_MAX;

    for (int i = 0; i < out->count; i++) {
        nc_strlcpy(out->records[i].tool, g_tv.records[i].tool, sizeof(out->records[i].tool));
        out->records[i].output_len = g_tv.records[i].output_len;
        out->records[i].flagged = g_tv.records[i].flagged;
        out->records[i].age_s = now_s - g_tv.records[i].timestamp;
        if (out->records[i].age_s < 0)
            out->records[i].age_s = 0;
    }
}

void tool_verify_restore(const tool_verify_snapshot_t *snap) {
    tool_verify_free();
    if (!snap) {
        tool_verify_init();
        return;
    }

    g_tv.count = snap->count;
    g_tv.total_verified = snap->total_verified;
    g_tv.total_flagged = snap->total_flagged;
    g_tv.consecutive_empty = snap->consecutive_empty;

    if (g_tv.count < 0)
        g_tv.count = 0;
    if (g_tv.count > NC_TV_HISTORY_MAX)
        g_tv.count = NC_TV_HISTORY_MAX;

    long now_s = nc_wall_sec();
    for (int i = 0; i < g_tv.count; i++) {
        nc_strlcpy(g_tv.records[i].tool, snap->records[i].tool, sizeof(g_tv.records[i].tool));
        g_tv.records[i].output_len = snap->records[i].output_len;
        g_tv.records[i].flagged = snap->records[i].flagged;
        g_tv.records[i].timestamp = now_s - snap->records[i].age_s;
        if (g_tv.records[i].timestamp > now_s)
            g_tv.records[i].timestamp = now_s;
    }
}
