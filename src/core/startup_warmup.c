/*
 * startup_warmup.c — Session startup warmup / preheating
 *
 * Pre-establishes API connections and prepares context
 * before the first user message, reducing first-turn latency.
 */

#include "nevercode.h"
#include "defaults.h"
/* ── Types ── */

typedef struct {
    warmup_status_t status;
    long started_ms;
    long completed_ms;
    long timeout_ms;
    bool api_verified;
    bool config_loaded;
    bool workspace_scanned;
    bool project_docs_found;
    int discovered_tools;
} warmup_ctx_t;

static warmup_ctx_t g_warmup;

/* ── Init / Free ── */

void warmup_init(long timeout_ms) {
    memset(&g_warmup, 0, sizeof(g_warmup));
    g_warmup.timeout_ms = timeout_ms > 0 ? timeout_ms : PREWARM_TIMEOUT_MS;
}

void warmup_free(void) {
    g_warmup.status = WARMUP_IDLE;
}

/* ── Warmup steps ── */

void warmup_start(void) {
    g_warmup.started_ms = nc_mono_ms();
    g_warmup.status = WARMUP_RUNNING;
}

void warmup_mark_api_verified(void) {
    g_warmup.api_verified = true;
}
void warmup_mark_config_loaded(void) {
    g_warmup.config_loaded = true;
}
void warmup_mark_workspace_scanned(void) {
    g_warmup.workspace_scanned = true;
}
void warmup_mark_project_docs(void) {
    g_warmup.project_docs_found = true;
}
void warmup_set_tools(int count) {
    g_warmup.discovered_tools = count;
}

void warmup_complete(void) {
    g_warmup.completed_ms = nc_mono_ms();
    g_warmup.status = WARMUP_READY;
}

void warmup_cancel(void) {
    g_warmup.completed_ms = nc_mono_ms();
    g_warmup.status = WARMUP_CANCELLED;
}

void warmup_fail(void) {
    g_warmup.completed_ms = nc_mono_ms();
    g_warmup.status = WARMUP_FAILED;
}

/* ── Check timeout ── */

bool warmup_check_timeout(void) {
    if (g_warmup.status != WARMUP_RUNNING)
        return false;
    long elapsed = nc_mono_ms() - g_warmup.started_ms;
    if (elapsed > g_warmup.timeout_ms) {
        g_warmup.completed_ms = nc_mono_ms();
        g_warmup.status = WARMUP_TIMEOUT;
        return true;
    }
    return false;
}

/* ── Readiness check ── */

bool warmup_is_ready(void) {
    if (g_warmup.status == WARMUP_READY)
        return true;
    if (g_warmup.status == WARMUP_RUNNING) {
        warmup_check_timeout();
        if (g_warmup.api_verified && g_warmup.config_loaded) {
            warmup_complete();
            return true;
        }
    }
    return false;
}

/* ── Queries ── */

warmup_status_t warmup_status(void) {
    return g_warmup.status;
}

long warmup_elapsed_ms(void) {
    if (g_warmup.completed_ms > 0)
        return g_warmup.completed_ms - g_warmup.started_ms;
    if (g_warmup.started_ms > 0)
        return nc_mono_ms() - g_warmup.started_ms;
    return 0;
}

bool warmup_has_project_docs(void) {
    return g_warmup.project_docs_found;
}
int warmup_tool_count(void) {
    return g_warmup.discovered_tools;
}

const char *warmup_status_name(warmup_status_t s) {
    switch (s) {
    case WARMUP_IDLE:
        return "idle";
    case WARMUP_RUNNING:
        return "running";
    case WARMUP_READY:
        return "ready";
    case WARMUP_TIMEOUT:
        return "timeout";
    case WARMUP_FAILED:
        return "failed";
    case WARMUP_CANCELLED:
        return "cancelled";
    }
    return "unknown";
}

char *warmup_format(void) {
    size_t sz = WARMUP_FMT_BUF;
    char *buf = malloc(sz);
    if (!buf)
        return NULL;
    snprintf(buf, sz,
             "Startup warmup: %s (%ldms)\n"
             "  API: %s | Config: %s | Workspace: %s | Docs: %s\n"
             "  Tools discovered: %d\n",
             warmup_status_name(g_warmup.status), warmup_elapsed_ms(),
             g_warmup.api_verified ? "OK" : "pending", g_warmup.config_loaded ? "OK" : "pending",
             g_warmup.workspace_scanned ? "OK" : "pending",
             g_warmup.project_docs_found ? "found" : "none", g_warmup.discovered_tools);
    return buf;
}
