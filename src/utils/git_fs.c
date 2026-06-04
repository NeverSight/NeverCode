/*
 * git_fs.c — Git filesystem integration.
 *
 * Provides gitignore checking, git config parsing, and filesystem
 * helpers that respect the repository's ignore rules.
 */

#include "nevercode.h"

/* GIT_FS_MAX_PATTERNS, GIT_FS_LINE_BUF in defaults.h */

typedef struct {
    char *pattern;
    bool negated;
    bool dir_only;
} gitignore_rule_t;

static struct {
    gitignore_rule_t rules[GIT_FS_MAX_PATTERNS];
    int count;
    char *repo_root;
    bool loaded;
} g_gfs;

void git_fs_init(void) {
    memset(&g_gfs, 0, sizeof(g_gfs));
}

void git_fs_free(void) {
    for (int i = 0; i < g_gfs.count; i++)
        free(g_gfs.rules[i].pattern);
    free(g_gfs.repo_root);
    memset(&g_gfs, 0, sizeof(g_gfs));
}

static int _parse_line(const char *line, gitignore_rule_t *out) {
    while (*line == ' ' || *line == '\t')
        line++;

    if (!*line || *line == '#')
        return 0;

    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        len--;
    if (len == 0)
        return 0;

    out->negated = false;
    out->dir_only = false;

    const char *p = line;
    if (*p == '!') {
        out->negated = true;
        p++;
        len--;
    }

    if (len > 0 && p[len - 1] == '/') {
        out->dir_only = true;
        len--;
    }

    out->pattern = nc_xstrndup(p, len);
    return 1;
}

int git_fs_load_gitignore(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    char buf[GIT_FS_LINE_BUF];
    while (fgets(buf, sizeof(buf), f) && g_gfs.count < GIT_FS_MAX_PATTERNS) {
        gitignore_rule_t r;
        if (_parse_line(buf, &r))
            g_gfs.rules[g_gfs.count++] = r;
    }
    fclose(f);
    g_gfs.loaded = true;
    return 0;
}

static bool _fnmatch_simple(const char *pattern, const char *name) {
    while (*pattern && *name) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern)
                return true;
            while (*name) {
                if (_fnmatch_simple(pattern, name))
                    return true;
                name++;
            }
            return false;
        } else if (*pattern == '?') {
            pattern++;
            name++;
        } else {
            if (*pattern != *name)
                return false;
            pattern++;
            name++;
        }
    }
    while (*pattern == '*')
        pattern++;
    return !*pattern && !*name;
}

bool git_fs_is_ignored(const char *relpath) {
    if (!g_gfs.loaded || !relpath)
        return false;

    const char *basename = strrchr(relpath, '/');
    basename = basename ? basename + 1 : relpath;

    bool ignored = false;
    for (int i = 0; i < g_gfs.count; i++) {
        gitignore_rule_t *r = &g_gfs.rules[i];
        bool matches = false;

        if (strchr(r->pattern, '/'))
            matches = _fnmatch_simple(r->pattern, relpath);
        else
            matches = _fnmatch_simple(r->pattern, basename);

        if (matches) {
            ignored = !r->negated;
        }
    }
    return ignored;
}

int git_fs_rule_count(void) {
    return g_gfs.count;
}

bool git_fs_loaded(void) {
    return g_gfs.loaded;
}

char *git_fs_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "GitFS: rules=%d loaded=%s root=%s\n", g_gfs.count,
                   g_gfs.loaded ? "yes" : "no", g_gfs.repo_root ? g_gfs.repo_root : "(none)");
    return strbuf_detach(&sb);
}
