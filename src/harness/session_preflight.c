/*
 * session_preflight.c — Pre-session health validation.
 *
 * Runs a quick diagnostic at session start: git status, feature list
 * coverage, progress tracker state, and episode memory availability.
 * Produces a structured report so the agent (and user) knows the
 * project health before starting work.
 */

#include "harness.h"
#include "git_ops.h"

static struct {
    bool ran;
    preflight_report_t last;
    char cwd[NC_PATH_MAX];
    long ran_ms;
} g_pf;

/* ── Helpers ── */

static void git_branch_name(const char *cwd, char *out, size_t cap) {
    if (git_current_branch(cwd, out, (int)cap) != 0)
        out[0] = '\0';
}

static void git_last_commit(const char *cwd, char *out, size_t cap) {
    if (git_log_oneline(cwd, 1, out, (int)cap) != 0)
        out[0] = '\0';
}

static void git_recent_log(const char *cwd, char *out, size_t cap, int max_lines) {
    if (git_log_oneline(cwd, max_lines, out, (int)cap) != 0)
        out[0] = '\0';
}

/* ── Public API ── */

void session_preflight_init(void) {
    memset(&g_pf, 0, sizeof(g_pf));
}

void session_preflight_free(void) {
    memset(&g_pf, 0, sizeof(g_pf));
}

preflight_report_t session_preflight_run(void) {
    long t0 = nc_mono_ms();
    char cwd[NC_PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd)))
        nc_strlcpy(cwd, ".", sizeof(cwd));

    if (g_pf.ran && strcmp(g_pf.cwd, cwd) == 0 && (t0 - g_pf.ran_ms) <= PREFLIGHT_CACHE_TTL_MS)
        return g_pf.last;

    preflight_report_t r;
    memset(&r, 0, sizeof(r));

    if (git_is_repo(cwd)) {
        r.git_dirty_count = git_dirty_count(cwd);
        if (r.git_dirty_count < 0)
            r.git_dirty_count = 0;
        r.git_clean = (r.git_dirty_count == 0);
        git_branch_name(cwd, r.git_branch, sizeof(r.git_branch));
        git_last_commit(cwd, r.last_commit, sizeof(r.last_commit));
        git_recent_log(cwd, r.git_log, sizeof(r.git_log), PREFLIGHT_GIT_LOG_LINES);
    } else {
        r.git_dirty_count = 0;
        r.git_clean = true;
    }

    r.feature_total = feature_list_count();
    r.feature_passing = feature_list_passing_count();

    r.progress_pending = progress_tracker_pending_count();
    r.progress_done = progress_tracker_done_count();

    r.episode_count = episode_memory_count();

    if (cwd[0]) {
        char path[NC_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", cwd, PROGRESS_FILE);
        r.progress_file_exists = nc_path_exists(path);
        snprintf(path, sizeof(path), "%s/%s", cwd, FLIST_FILE);
        r.feature_file_exists = nc_path_exists(path);
    }

    if (r.git_dirty_count >= PREFLIGHT_GIT_DIRTY_DEGRADE)
        r.status = PREFLIGHT_DEGRADED;
    else if (r.git_dirty_count >= PREFLIGHT_GIT_DIRTY_WARN)
        r.status = PREFLIGHT_WARN;
    else
        r.status = PREFLIGHT_OK;

    r.elapsed_ms = nc_mono_ms() - t0;
    g_pf.ran = true;
    g_pf.last = r;
    nc_strlcpy(g_pf.cwd, cwd, sizeof(g_pf.cwd));
    g_pf.ran_ms = t0;
    return r;
}

const char *preflight_status_name(preflight_status_t s) {
    switch (s) {
    case PREFLIGHT_OK:
        return "ok";
    case PREFLIGHT_WARN:
        return "warn";
    case PREFLIGHT_DEGRADED:
        return "degraded";
    default:
        return "unknown";
    }
}

char *session_preflight_format(const preflight_report_t *r) {
    if (!r)
        return nc_xstrdup("(no preflight data)\n");

    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_appendf(&sb, "Session Preflight [%s] (%ldms)\n", preflight_status_name(r->status),
                   r->elapsed_ms);

    if (r->git_branch[0])
        strbuf_appendf(&sb, "  Branch: %s\n", r->git_branch);
    if (r->last_commit[0])
        strbuf_appendf(&sb, "  Latest: %s\n", r->last_commit);

    strbuf_appendf(&sb, "  Git: %s", r->git_clean ? "clean" : "dirty");
    if (!r->git_clean)
        strbuf_appendf(&sb, " (%d file%s)", r->git_dirty_count, r->git_dirty_count > 1 ? "s" : "");
    strbuf_append_str(&sb, "\n");

    if (r->feature_file_exists)
        strbuf_appendf(&sb, "  Features: %d/%d passing\n", r->feature_passing, r->feature_total);

    if (r->progress_file_exists)
        strbuf_appendf(&sb, "  Progress: %d done, %d pending\n", r->progress_done,
                       r->progress_pending);

    if (r->episode_count > 0)
        strbuf_appendf(&sb, "  Episodes: %d recorded\n", r->episode_count);

    char *result = sb.data ? nc_xstrdup(sb.data) : nc_xstrdup("");
    strbuf_free(&sb);
    return result;
}

char *session_preflight_context(const preflight_report_t *r) {
    if (!r)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_str(&sb, "<session_preflight>\n");

    if (r->git_branch[0])
        strbuf_appendf(&sb, "branch: %s\n", r->git_branch);
    strbuf_appendf(&sb, "working_tree: %s\n", r->git_clean ? "clean" : "dirty");
    if (!r->git_clean)
        strbuf_appendf(&sb, "dirty_files: %d\n", r->git_dirty_count);

    if (r->git_log[0]) {
        strbuf_append_str(&sb, "recent_commits:\n");
        strbuf_append_str(&sb, r->git_log);
        size_t log_len = strlen(r->git_log);
        if (log_len == 0 || r->git_log[log_len - 1] != '\n')
            strbuf_append_str(&sb, "\n");
    }

    if (r->feature_file_exists && r->feature_total > 0) {
        int failing = r->feature_total - r->feature_passing;
        strbuf_appendf(&sb, "features: %d/%d passing (%d remaining)\n", r->feature_passing,
                       r->feature_total, failing);
    }
    if (r->progress_file_exists && (r->progress_pending + r->progress_done) > 0)
        strbuf_appendf(&sb, "progress: %d pending, %d done\n", r->progress_pending,
                       r->progress_done);
    if (r->episode_count > 0)
        strbuf_appendf(&sb, "episodes: %d\n", r->episode_count);

    strbuf_append_str(&sb, "</session_preflight>\n");

    char *result = sb.data ? nc_xstrdup(sb.data) : nc_xstrdup("");
    strbuf_free(&sb);
    return result;
}
