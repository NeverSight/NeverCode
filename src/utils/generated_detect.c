/*
 * generated_detect.c — Detect generated/vendored/lock files.
 *
 * Identifies files that should be excluded from attribution, context
 * analysis, and diff statistics: lock files, minified bundles,
 * vendored directories, build outputs, etc.
 */

#include "nevercode.h"

static const char *LOCK_NAMES[] = {
    "package-lock.json", "yarn.lock",           "pnpm-lock.yaml", "bun.lockb",   "bun.lock",
    "composer.lock",     "gemfile.lock",        "cargo.lock",     "poetry.lock", "pipfile.lock",
    "shrinkwrap.json",   "npm-shrinkwrap.json", "go.sum",         "uv.lock",     NULL};

static const char *LOCK_EXTS[] = {".lock",      ".min.js",     ".min.css",      ".min.html",
                                  ".bundle.js", ".bundle.css", ".generated.ts", ".generated.js",
                                  ".d.ts",      ".pyc",        ".pyo",          ".map",
                                  NULL};

static const char *VENDOR_DIRS[] = {
    "/dist/",        "/build/",       "/out/",      "/target/", "/node_modules/", "/.next/",
    "/__pycache__/", "/vendor/",      "/.git/",     "/.svn/",   "/coverage/",     "/.nyc_output/",
    "/deps/",        "/third_party/", "/external/", NULL};

static const char *basename_ptr(const char *path) {
    const char *last = strrchr(path, '/');
    return last ? last + 1 : path;
}

bool generated_detect(const char *path) {
    if (!path || !path[0])
        return false;

    const char *bn = basename_ptr(path);

    for (int i = 0; LOCK_NAMES[i]; i++) {
        if (strcasecmp(bn, LOCK_NAMES[i]) == 0)
            return true;
    }

    for (int i = 0; LOCK_EXTS[i]; i++) {
        if (nc_ends_with_ci(path, LOCK_EXTS[i]))
            return true;
    }

    for (int i = 0; VENDOR_DIRS[i]; i++) {
        if (strstr(path, VENDOR_DIRS[i]) != NULL)
            return true;
    }

    return false;
}

bool generated_is_lock_file(const char *path) {
    if (!path)
        return false;
    const char *bn = basename_ptr(path);
    for (int i = 0; LOCK_NAMES[i]; i++) {
        if (strcasecmp(bn, LOCK_NAMES[i]) == 0)
            return true;
    }
    return nc_ends_with_ci(path, ".lock");
}

bool generated_is_vendor(const char *path) {
    if (!path)
        return false;
    for (int i = 0; VENDOR_DIRS[i]; i++) {
        if (strstr(path, VENDOR_DIRS[i]) != NULL)
            return true;
    }
    return false;
}
