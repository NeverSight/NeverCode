/*
 * token_budget.c — Per-turn token budget tracking
 *
 * Monitors how many output tokens are consumed per turn and decides
 * whether to auto-continue when the model hits max_tokens (output
 * truncation).
 *
 * Features:
 *   - Track per-turn output token usage
 *   - Detect diminishing returns (model repeating/stalling)
 *   - Generate continuation nudge messages
 *   - Configurable budget per turn
 */

#include "nevercode.h"

/* TOKEN_COMPLETION_THRESHOLD, TOKEN_DIMINISHING_THRESHOLD,
   TOKEN_DEFAULT_TURN_BUDGET in defaults.h */
#define COMPLETION_THRESHOLD TOKEN_COMPLETION_THRESHOLD
#define DIMINISHING_THRESHOLD TOKEN_DIMINISHING_THRESHOLD
#define MAX_CONTINUATIONS MAX_CONTINUATION_RETRIES

typedef struct {
    int continuation_count;
    int last_delta_tokens;
    int last_total_tokens;
    long started_at;
    int budget;
    bool active;
} budget_tracker_t;

static budget_tracker_t g_tracker = {0};

void token_budget_init(int budget) {
    g_tracker.budget = budget;
    g_tracker.active = (budget > 0);
    g_tracker.continuation_count = 0;
    g_tracker.last_delta_tokens = 0;
    g_tracker.last_total_tokens = 0;
    g_tracker.started_at = 0;
}

void token_budget_start_turn(void) {
    g_tracker.continuation_count = 0;
    g_tracker.last_delta_tokens = 0;
    g_tracker.last_total_tokens = 0;
    g_tracker.started_at = nc_time_now();
}

void token_budget_set(int budget) {
    g_tracker.budget = budget;
    g_tracker.active = (budget > 0);
}

int token_budget_get(void) {
    return g_tracker.budget;
}

bool token_budget_active(void) {
    return g_tracker.active;
}

typedef enum { BUDGET_CONTINUE = 0, BUDGET_STOP = 1 } budget_decision_t;

typedef struct {
    budget_decision_t decision;
    int continuation_count;
    int pct;
    int turn_tokens;
    int budget;
    bool diminishing_returns;
    char nudge_msg[NC_MSG_MAX];
} budget_check_result_t;

budget_check_result_t token_budget_check(int total_output_tokens) {
    budget_check_result_t r = {0};
    r.turn_tokens = total_output_tokens;
    r.budget = g_tracker.budget;

    if (!g_tracker.active || g_tracker.budget <= 0) {
        r.decision = BUDGET_STOP;
        return r;
    }

    r.pct = g_tracker.budget > 0 ? (total_output_tokens * 100 / g_tracker.budget) : 0;

    int delta = total_output_tokens - g_tracker.last_total_tokens;

    bool is_diminishing = g_tracker.continuation_count >= 3 && delta < DIMINISHING_THRESHOLD &&
                          g_tracker.last_delta_tokens < DIMINISHING_THRESHOLD;

    r.diminishing_returns = is_diminishing;

    if (!is_diminishing && total_output_tokens < (int)(g_tracker.budget * COMPLETION_THRESHOLD) &&
        g_tracker.continuation_count < MAX_CONTINUATIONS) {

        g_tracker.continuation_count++;
        g_tracker.last_delta_tokens = delta;
        g_tracker.last_total_tokens = total_output_tokens;

        r.decision = BUDGET_CONTINUE;
        r.continuation_count = g_tracker.continuation_count;

        snprintf(r.nudge_msg, sizeof(r.nudge_msg),
                 "You've used approximately %d%% of your output budget (%dk / %dk tokens). "
                 "Please continue with the remaining work.",
                 r.pct, total_output_tokens / 1000, g_tracker.budget / 1000);
        return r;
    }

    r.decision = BUDGET_STOP;
    r.continuation_count = g_tracker.continuation_count;
    return r;
}

int token_budget_continuation_count(void) {
    return g_tracker.continuation_count;
}

long token_budget_elapsed_s(void) {
    if (g_tracker.started_at == 0)
        return 0;
    return (long)(nc_time_now() - g_tracker.started_at);
}

/* Max-tokens output recovery — generate continuation message */
char *token_budget_continuation_msg(int attempt, int total_attempts) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb,
                   "[Output was truncated due to max tokens. Please continue your response "
                   "from where you left off. (continuation %d/%d)]",
                   attempt, total_attempts);
    return strbuf_detach(&sb);
}

/* Format budget status for display */
char *token_budget_format(int turn_output_tokens) {
    char buf[NC_LABEL_MAX];
    if (!g_tracker.active) {
        snprintf(buf, sizeof(buf), "Token budget: disabled (unlimited output)\n");
    } else {
        int pct = g_tracker.budget > 0 ? (turn_output_tokens * 100 / g_tracker.budget) : 0;
        snprintf(buf, sizeof(buf), "Token budget: %dk / %dk (%d%%) | continuations: %d\n",
                 turn_output_tokens / 1000, g_tracker.budget / 1000, pct,
                 g_tracker.continuation_count);
    }
    return nc_xstrdup(buf);
}
