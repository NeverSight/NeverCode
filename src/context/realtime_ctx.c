/*
 * realtime_ctx.c — Live environment probes for system-prompt context
 *
 * Gathers git state, working directory, user info, OS, and
 * timestamp on demand.  Injected into the system prompt so the
 * model knows the current environment without tool calls.
 */

#include "nevercode.h"
#include "git_ops.h"

typedef struct {
    char *key;
    char *value;
    long captured_at;
} env_probe_result_t;

static env_probe_result_t g_probes[ENV_PROBE_MAX];
static int g_probe_count = 0;
static long g_last_refresh = 0;

static void probe_set(const char *key, char *value) {
    for (int i = 0; i < g_probe_count; i++) {
        if (strcmp(g_probes[i].key, key) == 0) {
            free(g_probes[i].value);
            g_probes[i].value = value;
            g_probes[i].captured_at = nc_wall_sec();
            return;
        }
    }
    if (g_probe_count >= ENV_PROBE_MAX) {
        free(value);
        return;
    }
    g_probes[g_probe_count].key = nc_xstrdup(key);
    g_probes[g_probe_count].value = value;
    g_probes[g_probe_count].captured_at = nc_wall_sec();
    g_probe_count++;
}

void realtime_ctx_init(void) {
    g_probe_count = 0;
    g_last_refresh = 0;
}

void realtime_ctx_free(void) {
    for (int i = 0; i < g_probe_count; i++) {
        free(g_probes[i].key);
        free(g_probes[i].value);
    }
    g_probe_count = 0;
}

void realtime_ctx_refresh(void) {
    char branch_buf[NC_LABEL_MAX];
    if (git_current_branch(NULL, branch_buf, sizeof(branch_buf)) == 0 && branch_buf[0])
        probe_set("git_branch", nc_xstrdup(branch_buf));

    int dc = git_dirty_count(NULL);
    if (dc >= 0) {
        char dc_str[NC_NUM_MAX];
        snprintf(dc_str, sizeof(dc_str), "%d", dc);
        probe_set("git_dirty_count", nc_xstrdup(dc_str));
    }

    char commit_buf[NC_LABEL_MAX];
    if (git_log_format(NULL, 1, "%h %s", commit_buf, sizeof(commit_buf)) == 0 && commit_buf[0])
        probe_set("git_last_commit", nc_xstrdup(commit_buf));

    char cwd[NC_PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)))
        probe_set("cwd", nc_xstrdup(cwd));

    const char *user = nc_user();
    if (user)
        probe_set("user", nc_xstrdup(user));

    char hostname[NC_LABEL_MAX];
    if (gethostname(hostname, sizeof(hostname)) == 0)
        probe_set("hostname", nc_xstrdup(hostname));

    time_t now = nc_time_now();
    struct tm *tm = localtime(&now);
    char timebuf[NC_FMT_MAX];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S %Z", tm);
    probe_set("datetime", nc_xstrdup(timebuf));

#ifdef __APPLE__
    probe_set("os", nc_xstrdup("macOS"));
#elif defined(__linux__)
    probe_set("os", nc_xstrdup("Linux"));
#else
    probe_set("os", nc_xstrdup("POSIX"));
#endif

    g_last_refresh = (long)now;
}

const char *realtime_ctx_get(const char *key) {
    for (int i = 0; i < g_probe_count; i++)
        if (strcmp(g_probes[i].key, key) == 0)
            return g_probes[i].value;
    return NULL;
}

char *realtime_ctx_build_section(void) {
    if (g_probe_count == 0)
        realtime_ctx_refresh();

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "<environment_context>\n");

    const char *cwd = realtime_ctx_get("cwd");
    if (cwd)
        strbuf_appendf(&sb, "Working Directory: %s\n", cwd);

    const char *branch = realtime_ctx_get("git_branch");
    if (branch)
        strbuf_appendf(&sb, "Git Branch: %s\n", branch);

    const char *dirty = realtime_ctx_get("git_dirty_count");
    if (dirty)
        strbuf_appendf(&sb, "Uncommitted Changes: %s files\n", dirty);

    const char *commit = realtime_ctx_get("git_last_commit");
    if (commit)
        strbuf_appendf(&sb, "Last Commit: %s\n", commit);

    const char *dt = realtime_ctx_get("datetime");
    if (dt)
        strbuf_appendf(&sb, "Date/Time: %s\n", dt);

    const char *os = realtime_ctx_get("os");
    const char *usr = realtime_ctx_get("user");
    const char *host = realtime_ctx_get("hostname");
    if (os || usr || host) {
        strbuf_appendf(&sb, "System: %s", os ? os : "unknown");
        if (usr)
            strbuf_appendf(&sb, " user=%s", usr);
        if (host)
            strbuf_appendf(&sb, " host=%s", host);
        strbuf_append_str(&sb, "\n");
    }

    strbuf_append_str(&sb, "</environment_context>\n");
    return strbuf_detach(&sb);
}

bool realtime_ctx_stale(int max_age_s) {
    long now = nc_wall_sec();
    return (now - g_last_refresh) > max_age_s;
}

int realtime_ctx_probe_count(void) {
    return g_probe_count;
}
