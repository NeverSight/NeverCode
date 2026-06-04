#include "nevercode.h"

typedef struct {
    bool active;
    int budget, tokens_used, tool_calls, max_tool_calls;
    long start_time, deadline_ms;
    bool stopped, budget_exceeded;
    const char *prompt;
    query_stop_hook_fn stop_hooks[QUERY_MAX_STOP_HOOKS];
    void *stop_hook_ctx[QUERY_MAX_STOP_HOOKS];
    int num_stop_hooks;
} query_ctx_t;

static query_ctx_t g_qctx;

void query_mode_init(const char *prompt, int budget) {
    memset(&g_qctx, 0, sizeof(g_qctx));
    g_qctx.active = true;
    g_qctx.prompt = prompt;
    g_qctx.budget = budget > 0 ? budget : QUERY_DEFAULT_BUDGET;
    g_qctx.max_tool_calls = QUERY_MAX_TOOLS;
    g_qctx.start_time = nc_wall_sec();
}

void query_mode_free(void) {
    memset(&g_qctx, 0, sizeof(g_qctx));
}
bool query_mode_active(void) {
    return g_qctx.active;
}

void query_mode_set_max_tools(int m) {
    g_qctx.max_tool_calls = m > 0 ? m : QUERY_MAX_TOOLS;
}

int query_mode_add_stop_hook(query_stop_hook_fn fn, void *ctx) {
    if (!fn || g_qctx.num_stop_hooks >= QUERY_MAX_STOP_HOOKS)
        return -1;
    g_qctx.stop_hooks[g_qctx.num_stop_hooks] = fn;
    g_qctx.stop_hook_ctx[g_qctx.num_stop_hooks] = ctx;
    g_qctx.num_stop_hooks++;
    return 0;
}

bool query_mode_consume(int t) {
    g_qctx.tokens_used += t;
    if (g_qctx.tokens_used >= g_qctx.budget) {
        g_qctx.budget_exceeded = true;
        return true;
    }
    return false;
}

void query_mode_record_tool(void) {
    g_qctx.tool_calls++;
}

bool query_mode_should_stop(void) {
    if (g_qctx.stopped || g_qctx.budget_exceeded)
        return true;
    if (g_qctx.tool_calls >= g_qctx.max_tool_calls)
        return true;
    if (g_qctx.deadline_ms > 0 && (nc_wall_sec() - g_qctx.start_time) * 1000 >= g_qctx.deadline_ms)
        return true;
    return false;
}

void query_mode_stop(void) {
    g_qctx.stopped = true;
    for (int i = 0; i < g_qctx.num_stop_hooks; i++)
        if (g_qctx.stop_hooks[i])
            g_qctx.stop_hooks[i](g_qctx.stop_hook_ctx[i]);
}
int query_mode_budget(void) {
    return g_qctx.budget;
}
bool query_mode_over_budget(void) {
    return g_qctx.budget_exceeded;
}
long query_mode_elapsed_ms(void) {
    return (nc_wall_sec() - g_qctx.start_time) * 1000;
}

int query_mode_remaining(void) {
    int r = g_qctx.budget - g_qctx.tokens_used;
    return r > 0 ? r : 0;
}

char *query_mode_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb,
                   "Query Mode: %s\n"
                   "  Budget: %d/%d tokens (%.0f%%)\n"
                   "  Tools: %d/%d calls\n"
                   "  Elapsed: %ldms\n",
                   g_qctx.stopped           ? "stopped"
                   : g_qctx.budget_exceeded ? "budget exceeded"
                                            : "running",
                   g_qctx.tokens_used, g_qctx.budget,
                   g_qctx.budget > 0 ? 100.0 * g_qctx.tokens_used / g_qctx.budget : 0.0,
                   g_qctx.tool_calls, g_qctx.max_tool_calls, query_mode_elapsed_ms());
    return strbuf_detach(&sb);
}
