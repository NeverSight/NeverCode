/*
 * file_index.c — workspace path index for fast lookup and fuzzy search
 *
 * Recursively walks the workspace directory tree, respects .gitignore
 * patterns via git_fs, and builds an in-memory path list for O(1)
 * lookup and fuzzy matching.
 */

#include "defaults.h"
#include "nevercode.h"
#include <fnmatch.h>

static char *g_root = NULL;
static char **g_paths = NULL;
static int g_n = 0;
static int g_cap = 0;
static time_t g_built = 0;

/* ── Internal helpers ── */

static int idx_push(const char *relpath) {
    if (g_n >= FILE_INDEX_MAX_ENTRIES)
        return -1;
    if (g_n >= g_cap) {
        int nc = g_cap ? g_cap * 2 : 256;
        if (nc > FILE_INDEX_MAX_ENTRIES)
            nc = FILE_INDEX_MAX_ENTRIES;
        char **np = realloc(g_paths, sizeof(char *) * (size_t)nc);
        if (!np)
            return -1;
        g_paths = np;
        g_cap = nc;
    }
    g_paths[g_n] = nc_xstrdup(relpath);
    if (!g_paths[g_n])
        return -1;
    g_n++;
    return 0;
}

static void walk_dir(const char *abs_base, const char *rel_prefix, int depth) {
    if (depth > FILE_INDEX_MAX_DEPTH)
        return;
    if (g_n >= FILE_INDEX_MAX_ENTRIES)
        return;

    DIR *d = opendir(abs_base);
    if (!d)
        return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (g_n >= FILE_INDEX_MAX_ENTRIES)
            break;

        const char *name = ent->d_name;
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
            continue;

        char relpath[NC_PATH_MAX];
        if (rel_prefix[0])
            snprintf(relpath, sizeof(relpath), "%s/%s", rel_prefix, name);
        else
            nc_strlcpy(relpath, name, sizeof(relpath));

        if (git_fs_loaded() && git_fs_is_ignored(relpath))
            continue;

        char abspath[NC_PATH_MAX];
        snprintf(abspath, sizeof(abspath), "%s/%s", abs_base, name);

        struct stat st;
        if (lstat(abspath, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            if (nc_is_noise_dir(name))
                continue;
            walk_dir(abspath, relpath, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            idx_push(relpath);
        }
    }
    closedir(d);
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

/* ── Public API ── */

void file_index_init(void) {
    file_index_free();
}

void file_index_free(void) {
    for (int i = 0; i < g_n; i++)
        free(g_paths[i]);
    free(g_paths);
    g_paths = NULL;
    g_n = 0;
    g_cap = 0;
    free(g_root);
    g_root = NULL;
    g_built = 0;
}

int file_index_build(const char *root_dir) {
    file_index_free();
    if (!root_dir)
        return -1;

    struct stat st;
    if (stat(root_dir, &st) != 0 || !S_ISDIR(st.st_mode))
        return -1;

    g_root = nc_xstrdup(root_dir);
    if (!g_root)
        return -1;

    char gi_path[NC_PATH_MAX];
    snprintf(gi_path, sizeof(gi_path), "%s/.gitignore", root_dir);
    git_fs_init();
    git_fs_load_gitignore(gi_path);

    walk_dir(root_dir, "", 0);

    if (g_n > 1)
        qsort(g_paths, (size_t)g_n, sizeof(char *), cmp_str);

    g_built = nc_time_now();
    return g_n;
}

bool file_index_ready(void) {
    return g_root != NULL && g_n > 0;
}

int file_index_count(void) {
    return g_n;
}

long file_index_age_s(void) {
    if (g_built == 0)
        return -1;
    return (long)(nc_time_now() - g_built);
}

const char *file_index_root(void) {
    return g_root;
}

bool file_index_lookup(const char *rel_path) {
    if (!rel_path || g_n == 0)
        return false;

    int lo = 0, hi = g_n - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int cmp = strcmp(g_paths[mid], rel_path);
        if (cmp == 0)
            return true;
        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return false;
}

int file_index_glob(const char *pattern, const char **out, int max) {
    if (!pattern || !out || max <= 0 || g_n == 0)
        return 0;
    int n = 0;
    for (int i = 0; i < g_n && n < max; i++) {
        if (fnmatch(pattern, g_paths[i], FNM_PATHNAME) == 0)
            out[n++] = g_paths[i];
    }
    return n;
}

int file_index_fuzzy(const char *query, fuzzy_result_t *out, int max) {
    if (!query || !out || max <= 0 || g_n == 0)
        return 0;
    const char **items = (const char **)calloc((size_t)g_n, sizeof(const char *));
    if (!items)
        return 0;
    for (int i = 0; i < g_n; i++)
        items[i] = g_paths[i];
    int nf = fuzzy_filter(items, g_n, query, out, max);
    free(items);
    return nf;
}

int file_index_prefix(const char *prefix, const char **out, int max) {
    if (!prefix || !out || max <= 0 || g_n == 0)
        return 0;
    size_t plen = strlen(prefix);
    int n = 0;
    for (int i = 0; i < g_n && n < max; i++) {
        if (strncmp(g_paths[i], prefix, plen) == 0)
            out[n++] = g_paths[i];
    }
    return n;
}

char *file_index_format_stats(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "file_index: %d paths", g_n);
    if (g_root)
        strbuf_appendf(&sb, " root=%s", g_root);
    if (g_built)
        strbuf_appendf(&sb, " age_s=%ld", (long)file_index_age_s());
    return strbuf_detach(&sb);
}
