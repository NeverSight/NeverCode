/*
 * auto_test.c — Self-verification loop: detect and run tests after edits
 */
#include "harness.h"
typedef struct {
    char pattern[NC_LABEL_MAX];
    char command[NC_CMD_MAX];
    bool detected;
} atest_pat_t;
typedef struct {
    atest_pat_t pats[ATEST_MAX_PATTERNS];
    int count;
    char root[NC_PATH_MAX];
    int last_turn;
    int total_runs, total_pass, total_fail;
    long last_time;
    bool enabled;
} atest_ctx_t;
static atest_ctx_t g_at;
void auto_test_init(const char *root) {
    memset(&g_at, 0, sizeof(g_at));
    if (root && root[0])
        nc_strlcpy(g_at.root, root, sizeof(g_at.root));
    else
        nc_strlcpy(g_at.root, ".", sizeof(g_at.root));
    g_at.enabled = true;
    g_at.last_turn = -ATEST_COOLDOWN_TURNS;
}
void auto_test_free(void) {
    memset(&g_at, 0, sizeof(g_at));
}
void auto_test_set_enabled(bool e) {
    g_at.enabled = e;
}
bool auto_test_is_enabled(void) {
    return g_at.enabled;
}
static void at_detect(void) {
    if (g_at.count > 0)
        return;
    char p[NC_PATH_MAX];
    char mk[NC_PATH_MAX];
    snprintf(mk, sizeof(mk), "%.4084s/Makefile", g_at.root);
    bool has_make = nc_file_exists(mk);
    snprintf(p, sizeof(p), "%.4075s/tests/test_main.c", g_at.root);
    if (has_make && nc_file_exists(p)) {
        atest_pat_t *a = &g_at.pats[g_at.count++];
        nc_strlcpy(a->pattern, "tests/test_main.c", sizeof(a->pattern));
        nc_strlcpy(a->command, "make test-unit", sizeof(a->command));
        a->detected = true;
    } else if (has_make) {
        atest_pat_t *a = &g_at.pats[g_at.count++];
        nc_strlcpy(a->pattern, "Makefile", sizeof(a->pattern));
        nc_strlcpy(a->command, "make test", sizeof(a->command));
        a->detected = true;
    }
    snprintf(p, sizeof(p), "%.4080s/package.json", g_at.root);
    if (nc_file_exists(p)) {
        atest_pat_t *a = &g_at.pats[g_at.count++];
        nc_strlcpy(a->pattern, "package.json", sizeof(a->pattern));
        nc_strlcpy(a->command, "npm test", sizeof(a->command));
        a->detected = true;
    }
    snprintf(p, sizeof(p), "%.4082s/Cargo.toml", g_at.root);
    if (nc_file_exists(p)) {
        atest_pat_t *a = &g_at.pats[g_at.count++];
        nc_strlcpy(a->pattern, "Cargo.toml", sizeof(a->pattern));
        nc_strlcpy(a->command, "cargo test", sizeof(a->command));
        a->detected = true;
    }
    snprintf(p, sizeof(p), "%.4086s/go.mod", g_at.root);
    if (nc_file_exists(p)) {
        atest_pat_t *a = &g_at.pats[g_at.count++];
        nc_strlcpy(a->pattern, "go.mod", sizeof(a->pattern));
        nc_strlcpy(a->command, "go test ./...", sizeof(a->command));
        a->detected = true;
    }
    snprintf(p, sizeof(p), "%.4078s/pyproject.toml", g_at.root);
    if (nc_file_exists(p) && g_at.count < ATEST_MAX_PATTERNS) {
        atest_pat_t *a = &g_at.pats[g_at.count++];
        nc_strlcpy(a->pattern, "pyproject.toml", sizeof(a->pattern));
        nc_strlcpy(a->command, "python -m pytest", sizeof(a->command));
        a->detected = true;
    }
    snprintf(p, sizeof(p), "%.4084s/setup.py", g_at.root);
    if (nc_file_exists(p) && g_at.count < ATEST_MAX_PATTERNS) {
        atest_pat_t *a = &g_at.pats[g_at.count++];
        nc_strlcpy(a->pattern, "setup.py", sizeof(a->pattern));
        nc_strlcpy(a->command, "python -m pytest", sizeof(a->command));
        a->detected = true;
    }
    snprintf(p, sizeof(p), "%.4078s/CMakeLists.txt", g_at.root);
    if (nc_file_exists(p) && g_at.count < ATEST_MAX_PATTERNS) {
        atest_pat_t *a = &g_at.pats[g_at.count++];
        nc_strlcpy(a->pattern, "CMakeLists.txt", sizeof(a->pattern));
        nc_strlcpy(a->command, "cmake --build build --target test", sizeof(a->command));
        a->detected = true;
    }
    snprintf(p, sizeof(p), "%.4085s/pom.xml", g_at.root);
    if (nc_file_exists(p) && g_at.count < ATEST_MAX_PATTERNS) {
        atest_pat_t *a = &g_at.pats[g_at.count++];
        nc_strlcpy(a->pattern, "pom.xml", sizeof(a->pattern));
        nc_strlcpy(a->command, "mvn test", sizeof(a->command));
        a->detected = true;
    }
    snprintf(p, sizeof(p), "%.4080s/build.gradle", g_at.root);
    if (!nc_file_exists(p))
        snprintf(p, sizeof(p), "%.4076s/build.gradle.kts", g_at.root);
    if (nc_file_exists(p) && g_at.count < ATEST_MAX_PATTERNS) {
        atest_pat_t *a = &g_at.pats[g_at.count++];
        nc_strlcpy(a->pattern, "build.gradle", sizeof(a->pattern));
        nc_strlcpy(a->command, "gradle test", sizeof(a->command));
        a->detected = true;
    }
}
char *auto_test_find_test_file(const char *ef) {
    if (!ef)
        return NULL;
    const char *base = strrchr(ef, '/');
    base = base ? base + 1 : ef;
    const char *dot = strrchr(base, '.');
    if (!dot)
        return NULL;
    size_t sl = (size_t)(dot - base);
    char stem[NC_LABEL_MAX];
    if (sl >= sizeof(stem))
        return NULL;
    memcpy(stem, base, sl);
    stem[sl] = '\0';
    size_t dl = base > ef ? (size_t)(base - 1 - ef) : 0;
    char dp[NC_PATH_MAX];
    if (dl > 0 && dl < sizeof(dp)) {
        memcpy(dp, ef, dl);
        dp[dl] = '\0';
    } else {
        dp[0] = '.';
        dp[1] = '\0';
    }
    static const char *pf[] = {"test_", "spec_", ""};
    static const char *sf[] = {"_test", "_spec", ".test", ".spec", ""};
    static const char *td[] = {"tests", "test", "__tests__", "spec", ""};
    char c[NC_PATH_MAX];
    for (int pi = 0; pf[pi]; pi++)
        for (int si = 0; sf[si]; si++) {
            if (pf[pi][0] == '\0' && sf[si][0] == '\0')
                continue;
            snprintf(c, sizeof(c), "%s/%s%s%s%s", dp, pf[pi], stem, sf[si], dot);
            if (nc_file_exists(c))
                return nc_xstrdup(c);
            for (int di = 0; td[di][0]; di++) {
                snprintf(c, sizeof(c), "%.680s/%.680s/%.680s%.680s%.680s%.680s", g_at.root, td[di], pf[pi], stem, sf[si],
                         dot);
                if (nc_file_exists(c))
                    return nc_xstrdup(c);
            }
        }
    return NULL;
}
bool auto_test_should_run(int t) {
    if (!g_at.enabled)
        return false;
    return (t - g_at.last_turn) >= ATEST_COOLDOWN_TURNS;
}
char *auto_test_build_command(const char *ef) {
    at_detect();
    if (g_at.count == 0)
        return NULL;
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, g_at.pats[0].command);
    return strbuf_detach(&sb);
}
void auto_test_record_run(int t, bool ok) {
    g_at.last_turn = t;
    g_at.last_time = nc_wall_sec();
    g_at.total_runs++;
    if (ok)
        g_at.total_pass++;
    else
        g_at.total_fail++;
}
char *auto_test_suggest_prompt(const char *ef) {
    if (!g_at.enabled || !ef)
        return NULL;
    at_detect();
    if (g_at.count == 0)
        return NULL;
    char *cmd = auto_test_build_command(ef);
    if (!cmd)
        return NULL;
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "[Self-verify] File modified: %s\nSuggested test: %s", ef, cmd);
    free(cmd);
    return strbuf_detach(&sb);
}
int auto_test_run_count(void) {
    return g_at.total_runs;
}
int auto_test_pass_count(void) {
    return g_at.total_pass;
}
int auto_test_fail_count(void) {
    return g_at.total_fail;
}
char *auto_test_format_stats(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Auto-test: %d runs | %d pass | %d fail | %d patterns detected",
                   g_at.total_runs, g_at.total_pass, g_at.total_fail, g_at.count);
    return strbuf_detach(&sb);
}

void auto_test_snapshot(auto_test_snapshot_t *out) {
    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    long now_s = nc_wall_sec();
    out->count = g_at.count;
    nc_strlcpy(out->root, g_at.root, sizeof(out->root));
    out->last_turn = g_at.last_turn;
    out->total_runs = g_at.total_runs;
    out->total_pass = g_at.total_pass;
    out->total_fail = g_at.total_fail;
    out->enabled = g_at.enabled;

    if (out->count < 0)
        out->count = 0;
    if (out->count > ATEST_MAX_PATTERNS)
        out->count = ATEST_MAX_PATTERNS;

    if (g_at.total_runs > 0 && g_at.last_time > 0) {
        out->last_age_s = now_s - g_at.last_time;
        if (out->last_age_s < 0)
            out->last_age_s = 0;
    }

    for (int i = 0; i < out->count; i++) {
        nc_strlcpy(out->patterns[i].pattern, g_at.pats[i].pattern,
                   sizeof(out->patterns[i].pattern));
        nc_strlcpy(out->patterns[i].command, g_at.pats[i].command,
                   sizeof(out->patterns[i].command));
        out->patterns[i].detected = g_at.pats[i].detected;
    }
}

void auto_test_restore(const auto_test_snapshot_t *snap) {
    auto_test_free();
    if (!snap) {
        auto_test_init(NULL);
        return;
    }

    auto_test_init(snap->root[0] ? snap->root : NULL);
    g_at.last_turn = snap->last_turn;
    g_at.total_runs = snap->total_runs;
    g_at.total_pass = snap->total_pass;
    g_at.total_fail = snap->total_fail;
    g_at.enabled = snap->enabled;
    g_at.count = snap->count;

    if (g_at.count < 0)
        g_at.count = 0;
    if (g_at.count > ATEST_MAX_PATTERNS)
        g_at.count = ATEST_MAX_PATTERNS;

    if (g_at.total_runs > 0 && snap->last_age_s >= 0) {
        long now_s = nc_wall_sec();
        g_at.last_time = now_s - snap->last_age_s;
        if (g_at.last_time > now_s)
            g_at.last_time = now_s;
    }

    for (int i = 0; i < g_at.count; i++) {
        nc_strlcpy(g_at.pats[i].pattern, snap->patterns[i].pattern, sizeof(g_at.pats[i].pattern));
        nc_strlcpy(g_at.pats[i].command, snap->patterns[i].command, sizeof(g_at.pats[i].command));
        g_at.pats[i].detected = snap->patterns[i].detected;
    }
}
