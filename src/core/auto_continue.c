/*
 * auto_continue.c -- Automatic continuation for truncated model responses.
 *
 * When the model's output is cut off (finish_reason == "length"),
 * this module injects a continuation prompt and retries, stitching
 * the partial output back together seamlessly.
 */
#include "nevercode.h"
#include "defaults.h"

typedef enum { AC_IDLE = 0, AC_AWAITING, AC_CONTINUING, AC_DONE } ac_phase_t;

typedef struct {
    ac_phase_t phase;
    int max_retries;
    int retries;
    int total_continuations;
    bool enabled;
    bool last_was_truncated;
    strbuf_t accumulated;
    int stitched_count;
    int escalation_count;
    bool escalation_pending;
} ac_ctx_t;

static ac_ctx_t g_ac;

void auto_continue_init(int max_retries) {
    memset(&g_ac, 0, sizeof(g_ac));
    g_ac.max_retries = max_retries > 0 ? max_retries : MAX_CONTINUATION_RETRIES;
    g_ac.enabled = true;
    strbuf_init(&g_ac.accumulated);
}

void auto_continue_free(void) {
    strbuf_free(&g_ac.accumulated);
    memset(&g_ac, 0, sizeof(g_ac));
}

bool auto_continue_enabled(void) {
    return g_ac.enabled;
}
void auto_continue_set_enabled(bool on) {
    g_ac.enabled = on;
}

bool auto_continue_should_retry(const char *finish_reason, const char *partial_text,
                                bool has_pending_tool_call) {
    if (!g_ac.enabled)
        return false;
    if (g_ac.retries >= g_ac.max_retries)
        return false;

    bool truncated = false;
    if (finish_reason && strcmp(finish_reason, FINISH_REASON_LENGTH) == 0)
        truncated = true;
    if (has_pending_tool_call && finish_reason && strcmp(finish_reason, FINISH_REASON_STOP) != 0)
        truncated = true;

    g_ac.last_was_truncated = truncated;
    if (!truncated) {
        g_ac.phase = AC_DONE;
        g_ac.retries = 0;
        return false;
    }

    if (partial_text)
        strbuf_append_str(&g_ac.accumulated, partial_text);

    g_ac.retries++;
    g_ac.total_continuations++;
    g_ac.phase = AC_CONTINUING;

    if (g_ac.escalation_count < NC_TOKEN_ESCALATION_MAX)
        g_ac.escalation_pending = true;
    return true;
}

bool auto_continue_needs_escalation(void) {
    return g_ac.escalation_pending;
}

int auto_continue_escalated_tokens(void) {
    return NC_ESCALATED_MAX_TOKENS;
}

void auto_continue_mark_escalated(void) {
    if (g_ac.escalation_pending) {
        g_ac.escalation_count++;
        g_ac.escalation_pending = false;
    }
}

const char *auto_continue_prompt(bool mid_tool_call) {
    return mid_tool_call ? AC_PROMPT_TOOL_CONTINUE : AC_PROMPT_CONTINUE;
}

void auto_continue_stitch(const char *continuation) {
    if (!continuation)
        return;
    strbuf_append_str(&g_ac.accumulated, continuation);
    g_ac.stitched_count++;
}

char *auto_continue_take_result(void) {
    if (g_ac.accumulated.len == 0)
        return NULL;
    char *result = strbuf_detach(&g_ac.accumulated);
    strbuf_init(&g_ac.accumulated);
    g_ac.phase = AC_DONE;
    g_ac.retries = 0;
    return result;
}

void auto_continue_reset(void) {
    strbuf_clear(&g_ac.accumulated);
    g_ac.phase = AC_IDLE;
    g_ac.retries = 0;
}

int auto_continue_retry_count(void) {
    return g_ac.retries;
}
int auto_continue_total_continuations(void) {
    return g_ac.total_continuations;
}
bool auto_continue_is_continuing(void) {
    return g_ac.phase == AC_CONTINUING;
}

char *auto_continue_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    const char *pn[] = {"idle", "awaiting", "continuing", "done"};
    strbuf_appendf(&sb,
                   "Auto-Continue: phase=%s retries=%d/%d\n"
                   "  total_continuations=%d stitched=%d escalations=%d\n"
                   "  accumulated_bytes=%d\n",
                   pn[g_ac.phase], g_ac.retries, g_ac.max_retries, g_ac.total_continuations,
                   g_ac.stitched_count, g_ac.escalation_count, (int)g_ac.accumulated.len);
    return strbuf_detach(&sb);
}
