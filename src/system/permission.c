#include "permission.h"
#include "tools.h"
#include "terminal.h"

void perm_init(perm_ctx_t *p) {
    memset(p, 0, sizeof(*p));
    p->trust_all = true;
    p->approval_preset = APPROVAL_FULL;
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

perm_decision_t perm_check(perm_ctx_t *p, const char *tool, const char *path) {
    if (p->trust_all)
        return PERM_ALLOW;

    /* Auto-allow read-only tools based on tool flags */
    const tool_def_t *td = tools_find(tool);
    if (td && td->read_only)
        return PERM_ALLOW;

    /* In read-only approval preset, deny non-read-only tools by default */
    if (p->approval_preset == APPROVAL_READONLY && td && !td->read_only)
        return PERM_ASK;

    /* Fallback whitelist when tools not yet registered or missing read_only */
    if (!td) {
        if (strcmp(tool, "Read") == 0 || strcmp(tool, "Glob") == 0 || strcmp(tool, "Grep") == 0 ||
            strcmp(tool, "ListDir") == 0 || strcmp(tool, "WebFetch") == 0 ||
            strcmp(tool, "WebSearch") == 0)
            return PERM_ALLOW;
    }
    if (strcmp(tool, "TodoWrite") == 0)
        return PERM_ALLOW;

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
    return PERM_ASK;
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
    case APPROVAL_FULL:
    case APPROVAL_DEFAULT:
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
