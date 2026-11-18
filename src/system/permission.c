#include "permission.h"
#include "tools.h"
#include "terminal.h"
#include <string.h> /* memcpy */

void perm_init(perm_ctx_t *p) {
    memset(p, 0, sizeof(*p));
    p->trust_all = false;
    p->approval_preset = APPROVAL_DEFAULT;
}

void perm_free(perm_ctx_t *p) {
    for (int i = 0; i < p->num_rules; i++) {
        free(p->rules[i].tool_name);
        free(p->rules[i].path_prefix);
    }
    free(p->rules);
    memset(p, 0, sizeof(*p));
}

void perm_add_rule(perm_ctx_t *p, const char *tool, const char *path_prefix, perm_decision_t d) {
    if (p->num_rules >= p->cap_rules) {
        int newcap = p->cap_rules ? p->cap_rules * 2 : 16;
        perm_rule_t *tmp = realloc(p->rules, newcap * sizeof(perm_rule_t));
        if (!tmp)
            return;
        p->rules = tmp;
        p->cap_rules = newcap;
    }
    perm_rule_t *r = &p->rules[p->num_rules++];
    r->tool_name = nc_xstrdup(tool);
    r->path_prefix = path_prefix ? nc_xstrdup(path_prefix) : NULL;
    r->decision = d;
}

/* Read-only tools that are safe to allow when no tool definition is available */
static bool _safe_readonly_fallback(const char *tool) {
    return strcmp(tool, "Read") == 0 || strcmp(tool, "Glob") == 0 || strcmp(tool, "Grep") == 0 ||
           strcmp(tool, "ListDir") == 0;
}

/* True if the tool performs network access (approval-required under "standard") */
static bool _is_network_tool(const char *tool) {
    return strcmp(tool, "WebFetch") == 0 || strcmp(tool, "WebSearch") == 0;
}

/* Resolve `path` the way the kernel does: walk each component in order and
 * realpath() each prefix that exists (resolving symlinks as we go). ".." pops
 * the last *resolved* component, which is the only correct behavior around
 * symlinks — lexical normalization alone gets this wrong. Non-existent
 * components are appended verbatim; absolute ".." at the root is a no-op.
 * The returned buffer is fully canonical when every component exists. */
static void _resolve_kernel_semantics(const char *path, char *out, size_t outsz) {
    char work[NC_PATH_MAX * 2];

    if (!path || !path[0]) {
        nc_strlcpy(out, "/", outsz);
        return;
    }
    if (path[0] != '/') {
        char cwd[NC_PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd)) ||
            snprintf(work, sizeof(work), "%s/%s", cwd, path) >= (int)sizeof(work))
            goto fallback;
    } else {
        if (snprintf(work, sizeof(work), "%s", path) >= (int)sizeof(work))
            goto fallback;
    }

    /* `work` is always absolute here, so the canonical prefix starts at root */
    char acc[NC_PATH_MAX * 2] = "/";
    char tmp[NC_PATH_MAX];
    char *save = NULL;
    for (char *tok = strtok_r(work, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        if (strcmp(tok, ".") == 0)
            continue;
        if (strcmp(tok, "..") == 0) {
            /* pop last resolved component */
            size_t len = strlen(acc);
            while (len > 1 && acc[len - 1] == '/')
                acc[--len] = '\0';
            char *slash = strrchr(acc, '/');
            if (slash)
                *slash = '\0';       /* keep "" (root) if no other slash */
            else if (len > 0) {
                acc[0] = '\0';       /* collapsed to root */
            }
            continue;
        }
        size_t alen = strlen(acc);
        size_t need = alen + 1 + strlen(tok) + 1;
        if (need >= sizeof(acc))
            goto fallback;
        if (alen == 0)
            snprintf(acc, sizeof(acc), "/%s", tok);
        else if (acc[alen - 1] != '/')
            snprintf(acc + alen, sizeof(acc) - alen, "/%s", tok);
        else
            snprintf(acc + alen, sizeof(acc) - alen, "%s", tok);
        if (realpath(acc, tmp))
            snprintf(acc, sizeof(acc), "%s", tmp);   /* canonical */
        /* else: leave non-existent components appended verbatim */
    }
    snprintf(out, outsz, "%s", acc[0] ? acc : "/");
    return;

fallback:
    nc_strlcpy(out, path, outsz);
}

/* True if `path` resolves inside the project workspace (or a scratch area). */
static bool _path_in_workspace(const char *path) {
    char resolved[NC_PATH_MAX];
    const char *root = project_root_detect(NULL);

    if (!path || !path[0])
        return true;

    _resolve_kernel_semantics(path, resolved, sizeof(resolved));

    if (root) {
        char root_res[NC_PATH_MAX];
        if (!realpath(root, root_res)) {
            /* unresolvable root: don't grant the carve-out */
        } else {
            size_t rlen = strlen(root_res);
            if (strncmp(resolved, root_res, rlen) == 0 &&
                (resolved[rlen] == '\0' || resolved[rlen] == '/'))
                return true;
        }
    }
    /* narrow device allowlist only */
    if (strcmp(resolved, "/dev/null") == 0 || strcmp(resolved, "/dev/zero") == 0 ||
        strcmp(resolved, "/dev/tty") == 0 || strcmp(resolved, "/dev/stdout") == 0 ||
        strcmp(resolved, "/dev/stderr") == 0)
        return true;
    if (strncmp(resolved, "/dev/fd/", 8) == 0)
        return true;
    if (strncmp(resolved, "/tmp/", 5) == 0 || strcmp(resolved, "/tmp") == 0)
        return true;
    const char *tdir = getenv("TMPDIR");
    if (tdir && tdir[0] && tdir[0] != '/') {
        /* relative TMPDIR: use as-is (matches tempfile semantics loosely) */
        char cwd[NC_PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
            char joined[NC_PATH_MAX + NC_PATH_MAX];
            if (snprintf(joined, sizeof(joined), "%s/%s", cwd, tdir) < (int)sizeof(joined)) {
                size_t jl = strlen(joined);
                while (jl > 1 && joined[jl - 1] == '/') joined[--jl] = '\0';
                size_t tlen = strlen(joined);
                if (strncmp(resolved, joined, tlen) == 0 &&
                    (resolved[tlen] == '\0' || resolved[tlen] == '/'))
                    return true;
            }
        }
        return false;
    }
    if (tdir && tdir[0] == '/') {
        /* canonical TMPDIR: require boundary, and "/" is never scratch space */
        if (strcmp(tdir, "/") == 0)
            return false;
        char tcanon[NC_PATH_MAX];
        if (!realpath(tdir, tcanon))
            return strncmp(resolved, tdir, strlen(tdir)) == 0 &&
                   (resolved[strlen(tdir)] == '\0' || resolved[strlen(tdir)] == '/');
        size_t tlen = strlen(tcanon);
        while (tlen > 1 && tcanon[tlen - 1] == '/') tcanon[--tlen] = '\0';
        tlen = strlen(tcanon);
        if (tlen == 0)
            return false;
        if (strncmp(resolved, tcanon, tlen) == 0 &&
            (resolved[tlen] == '\0' || resolved[tlen] == '/'))
            return true;
    }
    return false;
}

bool perm_path_in_workspace(const char *path) {
    return _path_in_workspace(path);
}

perm_decision_t perm_check(perm_ctx_t *p, const char *tool, const char *path) {
    if (p->trust_all)
        return PERM_ALLOW;

    const tool_def_t *td = tools_find(tool);

    /* User rules override preset defaults (and the read-only auto-allow) */
    for (int i = p->num_rules - 1; i >= 0; i--) {
        perm_rule_t *r = &p->rules[i];
        if (strcmp(r->tool_name, tool) != 0 && strcmp(r->tool_name, "*") != 0)
            continue;
        if (r->path_prefix && path) {
            size_t plen = strlen(r->path_prefix);
            bool ok = strncmp(path, r->path_prefix, plen) == 0;
            if (ok && plen > 0 && r->path_prefix[plen - 1] != '/')
                ok = path[plen] == '\0' || path[plen] == '/';
            if (!ok)
                continue;
        }
        return r->decision;
    }

    /* Auto-allow read-only tools based on tool flags (but network tools still ask) */
    if (td && td->read_only && !_is_network_tool(tool))
        return PERM_ALLOW;

    if (strcmp(tool, "TodoWrite") == 0)
        return PERM_ALLOW;

    /* In read-only approval preset, deny non-read-only tools by default */
    if (p->approval_preset == APPROVAL_READONLY) {
        if (!td && _safe_readonly_fallback(tool))
            return PERM_ALLOW;
        return PERM_ASK;
    }

    /* "standard" preset: workspace ops allowed silently; network access and
     * files outside the workspace require approval. Non-read-only tools with
     * no resolvable target require approval (executors should pass the
     * working directory / patch targets as the path hint). */
    if (p->approval_preset == APPROVAL_DEFAULT) {
        if (_is_network_tool(tool))
            return PERM_ASK;
        if (path)
            return _path_in_workspace(path) ? PERM_ALLOW : PERM_ASK;
        if (!td && _safe_readonly_fallback(tool))
            return PERM_ALLOW;
        return PERM_ASK;
    }

    /* full-access / unknown: permissive unless a rule denies */
    if (!td && !_safe_readonly_fallback(tool))
        return PERM_ASK;
    return PERM_ALLOW;
}

int perm_prompt_user(const char *tool, const char *detail) {
    term_color(T_YELLOW, "[permission] ");
    fprintf(stdout, "Allow %s", tool);
    if (detail && detail[0])
        fprintf(stdout, " (%s)", detail);
    fprintf(stdout, "? [y/N/a(lways)] ");
    fflush(stdout);

    char buf[NC_SHORT_MAX];
    if (!fgets(buf, sizeof(buf), stdin))
        return 0;
    if (buf[0] == 'a' || buf[0] == 'A')
        return 2;
    if (buf[0] == 'y' || buf[0] == 'Y')
        return 1;
    return 0;
}

void perm_set_approval(perm_ctx_t *p, approval_preset_t preset) {
    p->approval_preset = preset;
    switch (preset) {
    case APPROVAL_READONLY:
        p->trust_all = false;
        break;
    case APPROVAL_DEFAULT:
        /* "standard": workspace ops allowed; network/outside-workspace tools ask */
        p->trust_all = false;
        break;
    case APPROVAL_FULL:
    default:
        p->trust_all = true;
        break;
    }
}

int perm_parse_approval(const char *str) {
    if (!str)
        return -1;
    if (strcmp(str, "read-only") == 0 || strcmp(str, "readonly") == 0)
        return APPROVAL_READONLY;
    if (strcmp(str, "default") == 0 || strcmp(str, "auto") == 0)
        return APPROVAL_DEFAULT;
    if (strcmp(str, "full-access") == 0 || strcmp(str, "full") == 0)
        return APPROVAL_FULL;
    return -1;
}

const char *perm_approval_name(approval_preset_t preset) {
    switch (preset) {
    case APPROVAL_READONLY:
        return "read-only";
    case APPROVAL_FULL:
        return "full-access";
    case APPROVAL_DEFAULT:
    default:
        return "default";
    }
}
