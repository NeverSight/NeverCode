/*
 * diagnostics.c — Self-diagnostic health check system.
 *
 * Performs comprehensive environment and configuration validation,
 * checking API connectivity, tool availability, MCP servers, git state,
 * system resources, and config integrity.
 */

#include "nevercode.h"
#include <sys/statvfs.h>

/* DIAG_MAX_CHECKS in defaults.h */

typedef enum { CHECK_PASS = 0, CHECK_WARN, CHECK_FAIL, CHECK_SKIP } check_result_t;

typedef struct {
    char *category;
    char *name;
    char *detail;
    check_result_t result;
    long duration_ms;
} diag_check_t;

static struct {
    diag_check_t checks[DIAG_MAX_CHECKS];
    int count;
    long total_ms;
    bool ran;
} g_diag;

static void _add_check(const char *cat, const char *name, check_result_t result, const char *detail,
                       long ms) {
    if (g_diag.count >= DIAG_MAX_CHECKS)
        return;
    diag_check_t *c = &g_diag.checks[g_diag.count++];
    c->category = nc_xstrdup(cat);
    c->name = nc_xstrdup(name);
    c->detail = detail ? nc_xstrdup(detail) : NULL;
    c->result = result;
    c->duration_ms = ms;
}

void diagnostics_init(void) {
    memset(&g_diag, 0, sizeof(g_diag));
}

void sys_diagnostics_free(void) {
    for (int i = 0; i < g_diag.count; i++) {
        free(g_diag.checks[i].category);
        free(g_diag.checks[i].name);
        free(g_diag.checks[i].detail);
    }
    memset(&g_diag, 0, sizeof(g_diag));
}

static void _check_api_keys(void) {
    const char *oai = getenv(ENV_COMPAT_KEY);
    if (oai && strlen(oai) > 8)
        _add_check("API", "Compat API key", CHECK_PASS, "configured", 0);
    else
        _add_check("API", "Compat API key", CHECK_WARN, "not set — set " ENV_COMPAT_KEY, 0);

    const char *anth = getenv(ENV_NATIVE_KEY);
    if (anth && strlen(anth) > 8)
        _add_check("API", "Native API key", CHECK_PASS, "configured", 0);
    else
        _add_check("API", "Native API key", CHECK_WARN, "not set — set " ENV_NATIVE_KEY, 0);
}

static void _check_tools(void) {
    const char *tools[] = {"git", "curl", "grep", "find", NULL};
    for (int i = 0; tools[i]; i++) {
        long t0 = nc_mono_ms();
        bool found = nc_cmd_exists(tools[i]);
        long dt = nc_mono_ms() - t0;
        if (found)
            _add_check("Tools", tools[i], CHECK_PASS, "found in PATH", dt);
        else
            _add_check("Tools", tools[i], CHECK_WARN, "not found in PATH", dt);
    }
}

static void _check_git(void) {
    long t0 = nc_mono_ms();
    bool is_git = project_root_is_git();
    long dt = nc_mono_ms() - t0;
    if (is_git)
        _add_check("Git", "repository", CHECK_PASS, "inside git repo", dt);
    else
        _add_check("Git", "repository", CHECK_WARN, "not a git repository", dt);
}

static void _check_disk(void) {
    long t0 = nc_mono_ms();
    struct statvfs st;
    if (statvfs(".", &st) == 0) {
        long free_mb = (long)((unsigned long long)st.f_bavail * (unsigned long long)st.f_frsize /
                              (1024ULL * 1024ULL));
        long dt = nc_mono_ms() - t0;
        if (free_mb > RMON_LOW_DISK_MB) {
            char buf[NC_FMT_MAX];
            snprintf(buf, sizeof(buf), "%ld MB free", free_mb);
            _add_check("System", "disk space", CHECK_PASS, buf, dt);
        } else {
            char buf[NC_FMT_MAX];
            snprintf(buf, sizeof(buf), "only %ld MB free", free_mb);
            _add_check("System", "disk space", CHECK_WARN, buf, dt);
        }
    }
}

static void _check_config(void) {
    char path[NC_PATH_MAX];
    if (config_find_path(path, sizeof(path), NC_LAYER_CONFIG_FILE))
        _add_check("Config", "config file", CHECK_PASS, path, 0);
    else
        _add_check("Config", "config file", CHECK_WARN, "no config.json found", 0);
}

static void _check_project_docs(void) {
    char path[NC_PATH_MAX];
    const char *root = sandbox_root();
    if (!root)
        root = ".";

    bool found = false;
    for (int i = 0; i < nc_project_doc_agents_n && !found; i++) {
        snprintf(path, sizeof(path), "%s/%s", root, nc_project_doc_agents_paths[i]);
        if (access(path, R_OK) == 0) {
            _add_check("Config", "project docs", CHECK_PASS, nc_project_doc_agents_paths[i], 0);
            found = true;
        }
    }
    for (int i = 0; i < nc_project_doc_extra_n && !found; i++) {
        snprintf(path, sizeof(path), "%s/%s", root, nc_project_doc_extra_paths[i]);
        if (access(path, R_OK) == 0) {
            _add_check("Config", "project docs", CHECK_PASS, nc_project_doc_extra_paths[i], 0);
            found = true;
        }
    }
    if (!found)
        _add_check("Config", "project docs", CHECK_WARN,
                   "no project doc found (AGENTS.md, agents.md, or compatible config)", 0);
}

static void _check_state_db(void) {
    long t0 = nc_mono_ms();
    bool ok = state_db_ready();
    long dt = nc_mono_ms() - t0;
    if (ok) {
        char buf[NC_FMT_MAX];
        snprintf(buf, sizeof(buf), "%d sessions", state_db_sessions_count());
        _add_check("State", "database", CHECK_PASS, buf, dt);
    } else {
        _add_check("State", "database", CHECK_WARN, "not initialized", dt);
    }
}

int diagnostics_run(void) {
    sys_diagnostics_free();
    diagnostics_init();

    long t0 = nc_mono_ms();

    _check_api_keys();
    _check_tools();
    _check_git();
    _check_disk();
    _check_config();
    _check_project_docs();
    _check_state_db();

    g_diag.total_ms = nc_mono_ms() - t0;
    g_diag.ran = true;
    return g_diag.count;
}

int diagnostics_pass_count(void) {
    int n = 0;
    for (int i = 0; i < g_diag.count; i++)
        if (g_diag.checks[i].result == CHECK_PASS)
            n++;
    return n;
}

int diagnostics_warn_count(void) {
    int n = 0;
    for (int i = 0; i < g_diag.count; i++)
        if (g_diag.checks[i].result == CHECK_WARN)
            n++;
    return n;
}

int diagnostics_fail_count(void) {
    int n = 0;
    for (int i = 0; i < g_diag.count; i++)
        if (g_diag.checks[i].result == CHECK_FAIL)
            n++;
    return n;
}

int diagnostics_total(void) {
    return g_diag.count;
}

long diagnostics_elapsed_ms(void) {
    return g_diag.total_ms;
}

static const char *_result_name(check_result_t r) {
    switch (r) {
    case CHECK_PASS:
        return "PASS";
    case CHECK_WARN:
        return "WARN";
    case CHECK_FAIL:
        return "FAIL";
    case CHECK_SKIP:
        return "SKIP";
    }
    return "?";
}

char *diagnostics_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "=== Diagnostics (%d checks, %ldms) ===\n\n", g_diag.count,
                   g_diag.total_ms);

    const char *last_cat = "";
    for (int i = 0; i < g_diag.count; i++) {
        diag_check_t *c = &g_diag.checks[i];
        if (strcmp(c->category, last_cat) != 0) {
            strbuf_appendf(&sb, "\n[%s]\n", c->category);
            last_cat = c->category;
        }
        const char *icon = c->result == CHECK_PASS ? "+" : c->result == CHECK_WARN ? "!" : "x";
        strbuf_appendf(&sb, "  %s %-24s %s", icon, c->name, _result_name(c->result));
        if (c->detail)
            strbuf_appendf(&sb, " — %s", c->detail);
        if (c->duration_ms > 0)
            strbuf_appendf(&sb, " (%ldms)", c->duration_ms);
        strbuf_append_str(&sb, "\n");
    }

    int p = diagnostics_pass_count();
    int w = diagnostics_warn_count();
    int f = diagnostics_fail_count();
    strbuf_appendf(&sb, "\nSummary: %d pass, %d warn, %d fail\n", p, w, f);
    return strbuf_detach(&sb);
}

int diagnostics_exec(const cJSON *input, char **output, void *ctx) {
    (void)input;
    (void)ctx;
    diagnostics_run();
    *output = diagnostics_format();
    return 0;
}
