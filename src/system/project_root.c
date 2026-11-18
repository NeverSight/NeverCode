#include "nevercode.h"

/*
 * Project Root Auto-Detection — walk up from cwd to find project root
 * using marker files (.git, package.json, Cargo.toml, etc.)
 */

static const char *ROOT_MARKERS[] = {".git",
                                     "package.json",
                                     "Cargo.toml",
                                     "go.mod",
                                     "Makefile",
                                     "CMakeLists.txt",
                                     "pyproject.toml",
                                     "setup.py",
                                     "requirements.txt",
                                     "Gemfile",
                                     "pom.xml",
                                     "build.gradle",
                                     ".project",
                                     "tsconfig.json",
                                     "composer.json",
                                     "mix.exs",
                                     "Pipfile",
                                     "meson.build",
                                     "WORKSPACE",
                                     "BUILD.bazel",
                                     ".hg",
                                     ".svn",
                                     NULL};

static const char *PRIMARY_MARKERS[] = {".git",   "package.json",   "Cargo.toml",
                                        "go.mod", "pyproject.toml", NULL};

static char g_detected_root[NC_PATH_MAX] = {0};
static bool g_root_detected = false;

static bool has_marker(const char *dir, const char *marker) {
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%.2045s/%.2045s", dir, marker);
    struct stat st;
    return stat(path, &st) == 0;
}

const char *project_root_detect(const char *start_dir) {
    if (g_root_detected && g_detected_root[0])
        return g_detected_root;

    char dir[NC_PATH_MAX];
    if (start_dir)
        nc_strlcpy(dir, start_dir, sizeof(dir));
    else if (!getcwd(dir, sizeof(dir)))
        return NULL;

    char best_root[NC_PATH_MAX] = {0};
    int best_priority = 999;

    char current[NC_PATH_MAX];
    nc_strlcpy(current, dir, sizeof(current));

    while (1) {
        for (int i = 0; ROOT_MARKERS[i]; i++) {
            if (has_marker(current, ROOT_MARKERS[i])) {
                int pri = i;
                for (int j = 0; PRIMARY_MARKERS[j]; j++) {
                    if (strcmp(ROOT_MARKERS[i], PRIMARY_MARKERS[j]) == 0) {
                        pri = -1;
                        break;
                    }
                }
                if (pri < best_priority || !best_root[0]) {
                    nc_strlcpy(best_root, current, sizeof(best_root));
                    best_priority = pri;
                }
                if (strcmp(ROOT_MARKERS[i], ".git") == 0) {
                    nc_strlcpy(g_detected_root, current, sizeof(g_detected_root));
                    g_root_detected = true;
                    return g_detected_root;
                }
            }
        }

        char *slash = strrchr(current, '/');
        if (!slash || slash == current)
            break;
        *slash = '\0';
    }

    if (best_root[0]) {
        nc_strlcpy(g_detected_root, best_root, sizeof(g_detected_root));
        g_root_detected = true;
        return g_detected_root;
    }

    nc_strlcpy(g_detected_root, dir, sizeof(g_detected_root));
    g_root_detected = true;
    return g_detected_root;
}

void project_root_reset(void) {
    g_detected_root[0] = '\0';
    g_root_detected = false;
}

int project_root_markers_at(const char *dir, const char **out, int max) {
    if (!dir || !out || max <= 0)
        return 0;
    int count = 0;
    for (int i = 0; ROOT_MARKERS[i] && count < max; i++) {
        if (has_marker(dir, ROOT_MARKERS[i]))
            out[count++] = ROOT_MARKERS[i];
    }
    return count;
}

const char *project_root_primary_marker(const char *dir) {
    if (!dir)
        return NULL;
    for (int i = 0; PRIMARY_MARKERS[i]; i++) {
        if (has_marker(dir, PRIMARY_MARKERS[i]))
            return PRIMARY_MARKERS[i];
    }
    return NULL;
}

char *project_root_describe(void) {
    const char *root = project_root_detect(NULL);
    if (!root)
        return nc_xstrdup("(no project root detected)");

    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_str(&sb, "Project root: ");
    strbuf_append_str(&sb, root);
    strbuf_append_str(&sb, "\nMarkers: ");

    const char *markers[32];
    int n = project_root_markers_at(root, markers, 32);
    for (int i = 0; i < n; i++) {
        if (i > 0)
            strbuf_append_str(&sb, ", ");
        strbuf_append_str(&sb, markers[i]);
    }
    if (n == 0)
        strbuf_append_str(&sb, "(none)");
    strbuf_append_str(&sb, "\n");

    return strbuf_detach(&sb);
}

bool project_root_is_git(void) {
    const char *root = project_root_detect(NULL);
    if (!root)
        return false;
    return has_marker(root, ".git");
}

const char *project_root_list_markers(void) {
    static const char *list = ".git, package.json, Cargo.toml, go.mod, Makefile, "
                              "CMakeLists.txt, pyproject.toml, setup.py, requirements.txt, "
                              "Gemfile, pom.xml, build.gradle, .project, tsconfig.json, "
                              "composer.json, mix.exs, Pipfile, meson.build, WORKSPACE, "
                              "BUILD.bazel, .hg, .svn";
    return list;
}
