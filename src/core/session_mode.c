/*
 * session_mode.c — Persist and restore the active interaction mode across sessions
 *
 * When a session is created in coordinator or plan mode the active mode
 * is written into the session state file.  On resume the stored mode is
 * compared with the current runtime setting and, when they differ, the
 * runtime is adjusted to match so the session continues consistently.
 */

#include "nevercode.h"
#include "config.h"

typedef enum {
    SMODE_DEFAULT = 0,
    SMODE_COORDINATOR = 1,
    SMODE_PLAN = 2,
    SMODE_EXECUTE = 3,
    SMODE_PAIR = 4
} session_mode_t;

static session_mode_t g_mode = SMODE_DEFAULT;

void session_mode_set(int mode) {
    if (mode >= SMODE_DEFAULT && mode <= SMODE_PAIR)
        g_mode = (session_mode_t)mode;
}

int session_mode_get(void) {
    return (int)g_mode;
}

const char *session_mode_name(int mode) {
    switch ((session_mode_t)mode) {
    case SMODE_COORDINATOR:
        return "coordinator";
    case SMODE_PLAN:
        return "plan";
    case SMODE_EXECUTE:
        return "execute";
    case SMODE_PAIR:
        return "pair";
    default:
        return "default";
    }
}

int session_mode_parse(const char *name) {
    if (!name)
        return SMODE_DEFAULT;
    if (strcmp(name, "coordinator") == 0)
        return SMODE_COORDINATOR;
    if (strcmp(name, "plan") == 0)
        return SMODE_PLAN;
    if (strcmp(name, "execute") == 0)
        return SMODE_EXECUTE;
    if (strcmp(name, "pair") == 0)
        return SMODE_PAIR;
    return SMODE_DEFAULT;
}

int session_mode_save(const char *session_id) {
    if (!session_id)
        return -1;
    char dir[NC_PATH_MAX];
    config_home_path(dir, sizeof(dir), "sessions");

    nc_mkdir_p(dir);

    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%.2042s/%.2042s.mode", dir, session_id);

    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "%s\n", session_mode_name(g_mode));
    int rc = fflush(f) ? -1 : 0;
    fclose(f);
    return rc;
}

int session_mode_load(const char *session_id) {
    if (!session_id)
        return -1;
    char dir[NC_PATH_MAX];
    config_home_path(dir, sizeof(dir), "sessions");

    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%.2042s/%.2042s.mode", dir, session_id);

    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    char buf[NC_FMT_MAX] = {0};
    if (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
            buf[--len] = '\0';
        g_mode = (session_mode_t)session_mode_parse(buf);
    }
    fclose(f);
    return (int)g_mode;
}

bool session_mode_match(const char *session_id) {
    if (!session_id)
        return true;

    int stored = session_mode_load(session_id);
    if (stored < 0)
        return true;

    if (stored != (int)g_mode) {
        g_mode = (session_mode_t)stored;
        return false;
    }
    return true;
}

int session_mode_effort_hint(int mode) {
    switch ((session_mode_t)mode) {
    case SMODE_PLAN:
        return EFFORT_MEDIUM;
    case SMODE_EXECUTE:
        return EFFORT_HIGH;
    case SMODE_COORDINATOR:
        return EFFORT_HIGH;
    case SMODE_PAIR:
        return EFFORT_MEDIUM;
    default:
        return EFFORT_DEFAULT;
    }
}

bool session_mode_is_readonly(int mode) {
    return (session_mode_t)mode == SMODE_PLAN;
}

const char *session_mode_context_hint(int mode) {
    switch ((session_mode_t)mode) {
    case SMODE_PLAN:
        return "You are in plan mode (read-only). Explore the codebase, analyze options, "
               "and produce a structured plan. Do NOT make changes — only read, search, "
               "and reason. Output a numbered plan with rationale for each step.";
    case SMODE_EXECUTE:
        return "You are in execute mode (autonomous). Make assumptions and execute "
               "end-to-end without asking for confirmation. Complete the full task "
               "including verification. Prefer larger, confident changes over many small ones.";
    case SMODE_PAIR:
        return "You are in pair mode (collaborative). Work in small increments, "
               "showing your work frequently. Ask the user before making large "
               "or risky changes. Prefer smaller steps with check-ins.";
    case SMODE_COORDINATOR:
        return "You are in coordinator mode. Decompose goals into tasks and "
               "delegate work to sub-agents. Monitor progress and synthesize "
               "results. Do not perform file edits directly — delegate.";
    default:
        return NULL;
    }
}
