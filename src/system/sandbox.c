/*
 * sandbox.c — Path-level sandbox with opt-in enforcement.
 *
 * Two separate concerns:
 *   1) Project root tracking (always active, used by file_search/diagnostics)
 *   2) Path enforcement (opt-in via sandbox_enable(), off by default)
 *
 * When enforcement is off, sandbox_check_path() always returns true.
 * When enforcement is on, only paths within the project root, /tmp, /dev,
 * and user config directories are allowed.
 */

#include "nevercode.h"

static char g_sandbox_root[NC_PATH_MAX] = {0};
static bool g_root_set = false;
static bool g_enforcing = false;

void sandbox_init(const char *project_root) {
    if (!project_root) {
        char cwd[NC_PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)))
            project_root = cwd;
        else
            return;
    }

    char *resolved = realpath(project_root, NULL);
    if (resolved) {
        nc_strlcpy(g_sandbox_root, resolved, NC_PATH_MAX);
        free(resolved);
    } else {
        nc_strlcpy(g_sandbox_root, project_root, NC_PATH_MAX);
    }
    g_root_set = true;
}

void sandbox_enable(void) {
    g_enforcing = true;
}

void sandbox_disable(void) {
    g_enforcing = false;
}

bool sandbox_enforcing(void) {
    return g_enforcing;
}

static bool _path_under(const char *path, const char *prefix) {
    size_t plen = strlen(prefix);
    if (strncmp(path, prefix, plen) != 0)
        return false;
    return path[plen] == '\0' || path[plen] == '/';
}

bool sandbox_check_path(const char *path) {
    if (!g_enforcing)
        return true;

    if (!path || !path[0])
        return true;

    /* Relative paths are within project scope by convention */
    if (path[0] != '/')
        return true;

    /*
     * Detect ".." path traversal that could escape allowed directories.
     * We check the string directly (works even for non-existent paths),
     * then also try realpath() for existing paths with symlinks.
     */
    if (strstr(path, "/../") || (strlen(path) >= 3 && strcmp(path + strlen(path) - 3, "/..") == 0))
        return false;

    char resolved[NC_PATH_MAX];
    const char *check = path;
    if (realpath(path, resolved))
        check = resolved;

    /* Project root — but deny writes into .git/ and .nevercode/ (privilege escalation) */
    if (g_root_set && g_sandbox_root[0] && _path_under(check, g_sandbox_root)) {
        char gitdir[NC_PATH_MAX], cfgdir[NC_PATH_MAX];
        snprintf(gitdir, sizeof(gitdir), "%s/.git", g_sandbox_root);
        snprintf(cfgdir, sizeof(cfgdir), "%s/" CONFIG_DOTDIR_SECONDARY, g_sandbox_root);
        if (_path_under(check, gitdir) || _path_under(check, cfgdir))
            return false;
        return true;
    }

    /* /tmp, $TMPDIR, and /dev are always allowed */
    if (_path_under(check, "/tmp") || _path_under(check, "/private/tmp") ||
        _path_under(check, "/dev"))
        return true;
    const char *tmpdir = nc_tmpdir();
    if (tmpdir && _path_under(check, tmpdir))
        return true;

    /* Home config dirs: ~/.nevercode, ~/.agents, and compat dirs */
    const char *home = nc_home_dir();
    if (home) {
        char allowed[NC_PATH_MAX];
        static const char *config_dirs[] = {CONFIG_DOTDIR_SECONDARY, CONFIG_DOTDIR_COMPAT,
                                            CONFIG_DOTDIR_PRIMARY, ".config", NULL};
        for (int i = 0; config_dirs[i]; i++) {
            snprintf(allowed, sizeof(allowed), "%s/%s", home, config_dirs[i]);
            if (_path_under(check, allowed))
                return true;
        }
    }

    return false;
}

const char *sandbox_root(void) {
    return g_root_set ? g_sandbox_root : NULL;
}
