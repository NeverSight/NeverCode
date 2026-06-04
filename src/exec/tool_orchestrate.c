/*
 * tool_orchestrate.c — Unified tool execution pipeline
 *
 * Drives the full tool execution lifecycle:
 *   1) Permission check (fast-path for full-access mode)
 *   2) Sandbox strategy selection
 *   3) First execution attempt
 *   4) Failure analysis & optional escalation retry
 *   5) Result collection and reporting
 *
 * Tracks aggregate stats across all orchestrated calls.
 */

#include "nevercode.h"

typedef enum { SANDBOX_NONE = 0, SANDBOX_PATH_ONLY, SANDBOX_FULL } sandbox_strategy_t;

typedef struct {
    int calls;
    int successes;
    int retries;
    int denials;
    int escalations;
    int timeouts;
    int empty_results;
    long total_exec_ms;
    bool initialized;
} orch_stats_t;

static orch_stats_t g_orch;

void orch_init(void) {
    memset(&g_orch, 0, sizeof(g_orch));
    g_orch.initialized = true;
}

void orch_free(void) {
    g_orch.initialized = false;
}

/* ── Sandbox strategy selection ── */

static sandbox_strategy_t select_sandbox(const tool_def_t *tool) {
    if (!tool)
        return SANDBOX_NONE;

    if (!sandbox_enforcing())
        return SANDBOX_NONE;

    if (tool->read_only)
        return SANDBOX_NONE;

    if (tool->destructive)
        return SANDBOX_FULL;

    return SANDBOX_PATH_ONLY;
}

/* ── Permission check (full-access: always allow) ── */

static orch_result_t check_permissions(const tool_def_t *tool, cJSON *input, void *ctx) {
    (void)tool;
    (void)input;
    (void)ctx;
    /* full-access mode: skip all permission gates */
    return ORCH_OK;
}

/* ── Result validation (silent tool failures are the main reliability risk) ── */

static bool validate_result(const char *output, int rc) {
    if (rc != 0)
        return false;
    if (!output || output[0] == '\0')
        return false;
    return true;
}

/* ── Single execution attempt ── */

static int attempt_execute(const tool_def_t *tool, cJSON *input, char **output, void *ctx,
                           sandbox_strategy_t sb) {
    (void)sb;

    if (!tool->execute) {
        *output = nc_xstrdup("tool has no execute handler");
        return -1;
    }

    int rc = tool->execute(input, output, ctx);

    if (rc == 0 && !validate_result(*output, rc)) {
        g_orch.empty_results++;
        if (!*output)
            *output = nc_xstrdup("[empty result]");
    }

    return rc;
}

/* ── Retry with exponential backoff ── */

static int retry_with_backoff(const tool_def_t *tool, cJSON *input, char **output, void *ctx,
                              sandbox_strategy_t sandbox, int max_retries) {
    for (int attempt = 0; attempt <= max_retries; attempt++) {
        int rc = attempt_execute(tool, input, output, ctx, sandbox);
        if (rc == 0)
            return 0;

        if (attempt < max_retries) {
            g_orch.retries++;
            if (*output) {
                free(*output);
                *output = NULL;
            }
            int delay_ms =
                nc_backoff_exp2_sat(ORCH_RETRY_BASE_MS, ORCH_RETRY_MAX_DELAY_MS, attempt);
            usleep((useconds_t)(delay_ms * 1000));
        }
    }

    return -1;
}

/* ── Main orchestration entry ── */

orch_result_t orch_execute(const tool_def_t *tool, cJSON *input, char **output, void *ctx) {
    if (!g_orch.initialized || !tool || !output)
        return ORCH_ERROR;

    *output = NULL;
    g_orch.calls++;
    long t0 = nc_mono_ms();

    /* Step 1: Permission check */
    orch_result_t perm = check_permissions(tool, input, ctx);
    if (perm != ORCH_OK) {
        g_orch.denials++;
        *output = nc_xstrdup("permission denied");
        g_orch.total_exec_ms += (nc_mono_ms() - t0);
        return ORCH_DENIED;
    }

    /* Step 2: Select sandbox strategy */
    sandbox_strategy_t sandbox = select_sandbox(tool);

    /* Step 3: First attempt */
    int rc = attempt_execute(tool, input, output, ctx, sandbox);
    if (rc == 0) {
        g_orch.successes++;
        g_orch.total_exec_ms += (nc_mono_ms() - t0);
        return ORCH_OK;
    }

    /* Step 4: Escalation — retry without sandbox constraints */
    if (sandbox != SANDBOX_NONE) {
        g_orch.escalations++;
        if (*output) {
            free(*output);
            *output = NULL;
        }
        rc = retry_with_backoff(tool, input, output, ctx, SANDBOX_NONE, ORCH_MAX_RETRIES);
        if (rc == 0) {
            g_orch.successes++;
            g_orch.total_exec_ms += (nc_mono_ms() - t0);
            return ORCH_OK;
        }
    } else {
        /* Retry with same sandbox */
        if (*output) {
            free(*output);
            *output = NULL;
        }
        rc = retry_with_backoff(tool, input, output, ctx, sandbox, ORCH_MAX_RETRIES);
        if (rc == 0) {
            g_orch.successes++;
            g_orch.total_exec_ms += (nc_mono_ms() - t0);
            return ORCH_OK;
        }
    }

    /* Step 5: All retries exhausted */
    g_orch.total_exec_ms += (nc_mono_ms() - t0);

    long elapsed = nc_mono_ms() - t0;
    if (elapsed > ORCH_TIMEOUT_DEFAULT_MS) {
        g_orch.timeouts++;
        return ORCH_TIMEOUT;
    }

    return ORCH_RETRY_EXHAUSTED;
}

/* ── Batch execution ── */

int orch_execute_batch(const tool_def_t **tools, cJSON **inputs, char ***outputs, int count,
                       void *ctx) {
    if (!tools || !inputs || !outputs || count <= 0)
        return -1;

    *outputs = calloc((size_t)count, sizeof(char *));
    if (!*outputs)
        return -1;
    int failures = 0;

    for (int i = 0; i < count; i++) {
        char *out = NULL;
        orch_result_t r = orch_execute(tools[i], inputs[i], &out, ctx);
        (*outputs)[i] = out;
        if (r != ORCH_OK)
            failures++;
    }

    return failures;
}

/* ── Stats ── */

int orch_total_calls(void) {
    return g_orch.calls;
}
int orch_total_successes(void) {
    return g_orch.successes;
}
int orch_total_empty_results(void) {
    return g_orch.empty_results;
}

const char *orch_result_name(orch_result_t r) {
    switch (r) {
    case ORCH_OK:
        return "ok";
    case ORCH_DENIED:
        return "denied";
    case ORCH_TIMEOUT:
        return "timeout";
    case ORCH_ERROR:
        return "error";
    case ORCH_RETRY_EXHAUSTED:
        return "retry_exhausted";
    default:
        return "unknown";
    }
}

char *orch_format_stats(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb,
                   "Orchestrator: calls=%d ok=%d retries=%d denials=%d "
                   "escalations=%d timeouts=%d empty=%d exec_ms=%ld\n",
                   g_orch.calls, g_orch.successes, g_orch.retries, g_orch.denials,
                   g_orch.escalations, g_orch.timeouts, g_orch.empty_results, g_orch.total_exec_ms);
    return strbuf_detach(&sb);
}
