/*
 * session_resume.c — Full session reconstruction from persistent state.
 *
 * Rebuilds a live session from rollout events, restoring conversation
 * history, shell state, agent pool, MCP connections, and memory.
 */

#include "nevercode.h"

/* Macros sourced from defaults.h: RESUME_CTX_BUDGET, RESUME_MAX_EVENTS */

typedef enum {
    RESUME_IDLE = 0,
    RESUME_LOADING,
    RESUME_REPLAYING,
    RESUME_WIRING,
    RESUME_READY,
    RESUME_FAILED
} resume_phase_t;

static struct {
    resume_phase_t phase;
    char *session_id;
    char *resume_summary;
    char *reference_ctx;
    int events_replayed;
    int msgs_restored;
    int tools_restored;
    long started_ms;
    long elapsed_ms;
    bool memory_loaded;
    bool shell_restored;
    bool mcp_reconnected;
} g_resume;

static void _free_field(char **p) {
    if (*p) {
        free(*p);
        *p = NULL;
    }
}

void session_resume_init(void) {
    memset(&g_resume, 0, sizeof(g_resume));
    g_resume.phase = RESUME_IDLE;
}

void session_resume_free(void) {
    _free_field(&g_resume.session_id);
    _free_field(&g_resume.resume_summary);
    _free_field(&g_resume.reference_ctx);
    memset(&g_resume, 0, sizeof(g_resume));
}

bool session_resume_ready(void) {
    return g_resume.phase == RESUME_READY;
}

const char *session_resume_summary(void) {
    return g_resume.resume_summary;
}

const char *session_resume_reference(void) {
    return g_resume.reference_ctx;
}

int session_resume_msgs_restored(void) {
    return g_resume.msgs_restored;
}

long session_resume_elapsed_ms(void) {
    return g_resume.elapsed_ms;
}

bool session_resume_memory_loaded(void) {
    return g_resume.memory_loaded;
}

static const char *_phase_name(resume_phase_t p) {
    switch (p) {
    case RESUME_IDLE:
        return "idle";
    case RESUME_LOADING:
        return "loading";
    case RESUME_REPLAYING:
        return "replaying";
    case RESUME_WIRING:
        return "wiring";
    case RESUME_READY:
        return "ready";
    case RESUME_FAILED:
        return "failed";
    }
    return "unknown";
}

char *session_resume_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Resume: phase=%s session=%s msgs=%d events=%d",
                   _phase_name(g_resume.phase),
                   g_resume.session_id ? g_resume.session_id : "(none)", g_resume.msgs_restored,
                   g_resume.events_replayed);
    if (g_resume.elapsed_ms > 0)
        strbuf_appendf(&sb, " time=%ldms", g_resume.elapsed_ms);
    if (g_resume.memory_loaded)
        strbuf_append_str(&sb, " mem=ok");
    if (g_resume.shell_restored)
        strbuf_append_str(&sb, " shell=ok");
    strbuf_append_str(&sb, "\n");
    return strbuf_detach(&sb);
}
