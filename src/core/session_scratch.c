/*
 * session_scratch.c — Per-session temporary directory management.
 *
 * Provides an isolated temp directory for each session, separate from
 * the user's project.  The directory is created on demand and cleaned
 * up when the session ends.
 */

#include "nevercode.h"

static char g_scratch_path[NC_PATH_MAX];
static bool g_active = false;

int session_scratch_init(const char *session_id) {
    if (g_active)
        return 0;

    const char *home = config_home_dir();
    if (!home)
        return -1;

    const char *sid = session_id;
    char fallback[NC_FMT_MAX];
    if (!sid || !sid[0]) {
        snprintf(fallback, sizeof(fallback), "tmp-%ld", (long)getpid());
        sid = fallback;
    }

    snprintf(g_scratch_path, sizeof(g_scratch_path), "%s/" SESSION_SCRATCH_SUBDIR "/%s", home, sid);

    if (mkdir(g_scratch_path, NC_DIR_MODE_PRIVATE) != 0 && errno != EEXIST) {
        char parent[NC_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s/" SESSION_SCRATCH_SUBDIR, home);
        mkdir(parent, NC_DIR_MODE_PRIVATE);
        if (mkdir(g_scratch_path, NC_DIR_MODE_PRIVATE) != 0 && errno != EEXIST)
            return -1;
    }

    g_active = true;
    return 0;
}

const char *session_scratch_dir(void) {
    return g_active ? g_scratch_path : NULL;
}

bool session_scratch_active(void) {
    return g_active;
}

char *session_scratch_file(const char *name) {
    if (!g_active || !name)
        return NULL;
    char *path = malloc(NC_PATH_MAX);
    if (!path)
        return NULL;
    snprintf(path, NC_PATH_MAX, "%s/%s", g_scratch_path, name);
    return path;
}

void session_scratch_cleanup(void) {
    if (!g_active)
        return;
    nc_rm_rf(g_scratch_path);
    g_scratch_path[0] = '\0';
    g_active = false;
}

char *session_scratch_prompt(void) {
    if (!g_active)
        return NULL;
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "# Scratch Directory\n\n"
                           "Use this per-session scratch directory for ALL temporary file needs "
                           "instead of /tmp or other system temp directories:\n`");
    strbuf_append_str(&sb, g_scratch_path);
    strbuf_append_str(&sb, "`\n\n"
                           "Use it for intermediate results, temporary scripts, analysis outputs, "
                           "and any file that does not belong in the user's project.  "
                           "The directory is session-scoped and isolated from the project tree.\n");
    return strbuf_detach(&sb);
}
