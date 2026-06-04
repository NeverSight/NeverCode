/*
 * input_guard.c -- Input sanitization for tool parameters.
 *
 * Guards against path traversal, symlink escape, null bytes,
 * and oversized inputs before tools process them.
 */
#include "nevercode.h"
#include "defaults.h"

#define IG_MAX_COMPONENTS NC_INPUT_GUARD_COMPONENTS

typedef enum {
    IG_OK = 0,
    IG_NULL_BYTE,
    IG_TRAVERSAL,
    IG_SYMLINK_ESCAPE,
    IG_TOO_LONG,
    IG_ABSOLUTE_DENIED,
    IG_INVALID
} ig_result_t;

static struct {
    char root[NC_PATH_MAX];
    bool allow_absolute;
    bool follow_symlinks;
    bool initialized;
    int blocked_count;
    int checked_count;
} g_ig;

void input_guard_init(const char *project_root) {
    memset(&g_ig, 0, sizeof(g_ig));
    if (project_root)
        nc_strlcpy(g_ig.root, project_root, sizeof(g_ig.root));
    else
        getcwd(g_ig.root, sizeof(g_ig.root));
    g_ig.allow_absolute = true;
    g_ig.follow_symlinks = true; /* no symlink restrictions */
    g_ig.initialized = true;
}

void input_guard_free(void) {
    memset(&g_ig, 0, sizeof(g_ig));
}
void input_guard_set_allow_absolute(bool on) {
    g_ig.allow_absolute = on;
}

static bool has_null_byte(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (s[i] == '\0' && i < len - 1)
            return true;
    return false;
}

static bool has_traversal(const char *path) {
    const char *p = path;
    while (*p) {
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0'))
            return true;
        p = strchr(p, '/');
        if (!p)
            break;
        p++;
    }
    return false;
}

static ig_result_t check_path_internal(const char *path) {
    if (!path)
        return IG_INVALID;

    size_t len = strlen(path);
    if (len == 0)
        return IG_INVALID;
    if (len >= NC_PATH_MAX)
        return IG_TOO_LONG;
    if (has_null_byte(path, len))
        return IG_NULL_BYTE;

    if (!g_ig.allow_absolute && path[0] == '/')
        return IG_ABSOLUTE_DENIED;

    if (has_traversal(path))
        return IG_TRAVERSAL;

    if (!g_ig.follow_symlinks) {
        struct stat st;
        if (lstat(path, &st) == 0 && S_ISLNK(st.st_mode)) {
            char resolved[NC_PATH_MAX];
            if (realpath(path, resolved)) {
                if (!nc_starts_with(resolved, g_ig.root))
                    return IG_SYMLINK_ESCAPE;
            }
        }
    }

    return IG_OK;
}

bool input_guard_check_path(const char *path) {
    if (!g_ig.initialized)
        return true;
    g_ig.checked_count++;
    ig_result_t r = check_path_internal(path);
    if (r != IG_OK) {
        g_ig.blocked_count++;
        return false;
    }
    return true;
}

char *input_guard_resolve(const char *path, char *out, size_t out_sz) {
    if (!path || !out || out_sz == 0)
        return NULL;
    if (!input_guard_check_path(path))
        return NULL;

    if (path[0] == '/') {
        nc_strlcpy(out, path, out_sz);
    } else {
        snprintf(out, out_sz, "%s/%s", g_ig.root, path);
    }
    return out;
}

bool input_guard_check_length(const char *input, size_t max) {
    if (!input)
        return true;
    return strlen(input) <= max;
}

static const char *result_name(ig_result_t r) {
    switch (r) {
    case IG_OK:
        return "ok";
    case IG_NULL_BYTE:
        return "null_byte";
    case IG_TRAVERSAL:
        return "path_traversal";
    case IG_SYMLINK_ESCAPE:
        return "symlink_escape";
    case IG_TOO_LONG:
        return "too_long";
    case IG_ABSOLUTE_DENIED:
        return "absolute_denied";
    case IG_INVALID:
        return "invalid";
    default:
        return "unknown";
    }
}

const char *input_guard_diagnose(const char *path) {
    ig_result_t r = check_path_internal(path);
    return result_name(r);
}

int input_guard_blocked_count(void) {
    return g_ig.blocked_count;
}
int input_guard_checked_count(void) {
    return g_ig.checked_count;
}

char *input_guard_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb,
                   "Input Guard: root=%s\n"
                   "  allow_absolute=%s follow_symlinks=%s\n"
                   "  checked=%d blocked=%d\n",
                   g_ig.root, g_ig.allow_absolute ? "yes" : "no",
                   g_ig.follow_symlinks ? "yes" : "no", g_ig.checked_count, g_ig.blocked_count);
    return strbuf_detach(&sb);
}
