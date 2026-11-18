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

/* Normalize `path` lexically (resolves "." and ".."). Falls back to the raw
 * path on overflow. */
static void _normalize_lexical(const char *path, char *out, size_t outsz) {
    static const size_t MAX_PARTS = 2048;
    char tmp[NC_PATH_MAX * 2];
    if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp)) {
        nc_strlcpy(out, path, outsz);
        return;
    }
    const char *parts[2048];
    int np = 0;
    bool absolute = path[0] == '/';
    char *save = NULL;
    char *tok = strtok_r(tmp, "/", &save);
    while (tok) {
        if (strcmp(tok, ".") == 0)
            ; /* skip */
        else if (strcmp(tok, "..") == 0) {
            if (np > 0 && strcmp(parts[np - 1], "..") != 0)
                np--;
            else if (!absolute) {
                if (np >= (int)MAX_PARTS) {
                    nc_strlcpy(out, path, outsz);
                    return;
                }
                parts[np++] = tok;
            }
        } else {
            if (np >= (int)MAX_PARTS) {
                nc_strlcpy(out, path, outsz);
                return;
            }
            parts[np++] = tok;
        }
        tok = strtok_r(NULL, "/", &save);
    }
    size_t off = 0;
    out[0] = '\0';
    if (absolute && outsz > 1)
        out[off++] = '/';
    for (int i = 0; i < np && off < outsz - 1; i++) {
        if (i)
            out[off++] = '/';
        size_t tl = strlen(parts[i]);
        if (off + tl >= outsz)
            break;
        memcpy(out + off, parts[i], tl);
        off += tl;
    }
    out[off] = '\0';
}

/* True if `path` resolves inside the project workspace (or a scratch area). */
static bool _path_in_workspace(const char *path) {
    char resolved[NC_PATH_MAX];
    const char *root = project_root_detect(NULL);

    if (!path || !path[0])
        return true;

    bool absolute = path[0] == '/';
    if (realpath(path, resolved)) {
        /* have fully resolved */
    } else {
        char norm[NC_PATH_MAX * 2];
        char cwd[NC_PATH_MAX];
        if (!absolute && getcwd(cwd, sizeof(cwd))) {
            if (snprintf(norm, sizeof(norm), "%s/%s", cwd, path) < (int)sizeof(norm)) {
                char n2[NC_PATH_MAX * 2];
                _normalize_lexical(norm, n2, sizeof(n2));
                if (realpath(n2, resolved))
                    goto resolved_ok;
                nc_strlcpy(resolved, n2, sizeof(resolved));
                goto resolved_ok;
            }
        }
        {
            char n2[NC_PATH_MAX * 2];
            _normalize_lexical(path, n2, sizeof(n2));
            if (realpath(n2, resolved))
                goto resolved_ok;
            nc_strlcpy(resolved, n2, sizeof(resolved));
        }
    }
resolved_ok:

    if (root) {
        size_t rlen = strlen(root);
        if (strncmp(resolved, root, rlen) == 0 && (resolved[rlen] == '\0' || resolved[rlen] == '/'))
            return true;
    }
    if (strncmp(resolved, "/tmp/", 5) == 0 || strncmp(resolved, "/dev/", 5) == 0 ||
        strcmp(resolved, "/tmp") == 0 || strcmp(resolved, "/dev") == 0)
        return true;
    const char *tdir = getenv("TMPDIR");
    if (tdir && tdir[0] && strncmp(resolved, tdir, strlen(tdir)) == 0)
        return true;
    return false;
}

perm_decision_t perm_check(perm_ctx_t *p, const char *tool, const char *path) {
    if (p->trust_all)
        return PERM_ALLOW;

    /* Auto-allow read-only tools based on tool flags (but network tools still ask) */
    const tool_def_t *td = tools_find(tool);
    if (td && td->read_only && !_is_network_tool(tool))
        return PERM_ALLOW;

    /* User rules override preset defaults */
    for (int i = p->num_rules - 1; i >= 0; i--) {
        perm_rule_t *r = &p->rules[i];
        if (strcmp(r->tool_name, tool) != 0 && strcmp(r->tool_name, "*") != 0)
            continue;
        if (r->path_prefix && path) {
            if (!nc_starts_with(path, r->path_prefix))
                continue;
        }
        return r->decision;
    }

    if (strcmp(tool, "TodoWrite") == 0)
        return PERM_ALLOW;

    /* In read-only approval preset, deny non-read-only tools by default */
    if (p->approval_preset == APPROVAL_READONLY) {
        if (!td && _safe_readonly_fallback(tool))
            return PERM_ALLOW;
        return PERM_ASK;
    }

    /* "standard" preset: workspace ops allowed silently; network access and
     * files outside the workspace require approval */
    if (p->approval_preset == APPROVAL_DEFAULT) {
        if (_is_network_tool(tool))
            return PERM_ASK;
        if (path)
            return _path_in_workspace(path) ? PERM_ALLOW : PERM_ASK;
        return PERM_ALLOW;
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
