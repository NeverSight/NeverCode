/*
 * success_criteria.c — Declarative output validation via structured rules.
 *
 * A ruleset is a conjunction of typed predicates (output-contains,
 * file-exists, file-nonempty, exit-code-zero, etc.).  All rules must
 * pass for the overall evaluation to succeed.
 */

#include "harness.h"

void sc_ruleset_init(sc_ruleset_t *rs) {
    memset(rs, 0, sizeof(*rs));
}

static int _add(sc_ruleset_t *rs, sc_kind_t kind, const char *value, const char *path,
                bool case_sensitive) {
    if (rs->nrules >= SC_MAX_RULES)
        return -1;
    sc_rule_t *r = &rs->rules[rs->nrules++];
    r->kind = kind;
    r->case_sensitive = case_sensitive;
    if (value)
        nc_strlcpy(r->value, value, sizeof(r->value));
    if (path)
        nc_strlcpy(r->path, path, sizeof(r->path));
    return 0;
}

int sc_add_output_contains(sc_ruleset_t *rs, const char *needle, bool cs) {
    return _add(rs, SC_OUTPUT_CONTAINS, needle, NULL, cs);
}

int sc_add_output_not_contains(sc_ruleset_t *rs, const char *needle, bool cs) {
    return _add(rs, SC_OUTPUT_NOT_CONTAINS, needle, NULL, cs);
}

int sc_add_file_exists(sc_ruleset_t *rs, const char *path) {
    return _add(rs, SC_FILE_EXISTS, NULL, path, false);
}

int sc_add_file_contains(sc_ruleset_t *rs, const char *path, const char *needle, bool cs) {
    return _add(rs, SC_FILE_CONTAINS, needle, path, cs);
}

int sc_add_file_nonempty(sc_ruleset_t *rs, const char *path) {
    return _add(rs, SC_FILE_NONEMPTY, NULL, path, false);
}

int sc_add_exit_zero(sc_ruleset_t *rs) {
    return _add(rs, SC_EXIT_CODE_ZERO, NULL, NULL, false);
}

static char *_read_file(const char *dir, const char *rel) {
    char path[NC_PATH_MAX];
    if (dir && dir[0])
        snprintf(path, sizeof(path), "%s/%s", dir, rel);
    else
        nc_strlcpy(path, rel, sizeof(path));

    return strbuf_read_file_max(path, NULL, NC_READ_CAP_HARNESS);
}

bool sc_evaluate(const sc_ruleset_t *rs, const char *output, int exit_code, const char *workdir,
                 char *detail, size_t detail_sz) {
    if (rs->nrules == 0) {
        if (detail)
            snprintf(detail, detail_sz, "no rules configured");
        return true;
    }

    const char *out = output ? output : "";
    bool all_pass = true;
    strbuf_t sb;
    strbuf_init(&sb);

    for (int i = 0; i < rs->nrules; i++) {
        const sc_rule_t *r = &rs->rules[i];
        bool ok = false;

        switch (r->kind) {
        case SC_OUTPUT_CONTAINS:
            ok =
                r->case_sensitive ? (strstr(out, r->value) != NULL) : nc_contains_ci(out, r->value);
            strbuf_appendf(&sb, "%s(%s)=%s; ", sc_kind_name(r->kind), r->value,
                           ok ? "pass" : "FAIL");
            break;

        case SC_OUTPUT_NOT_CONTAINS:
            ok = r->case_sensitive ? (strstr(out, r->value) == NULL)
                                   : !nc_contains_ci(out, r->value);
            strbuf_appendf(&sb, "%s(%s)=%s; ", sc_kind_name(r->kind), r->value,
                           ok ? "pass" : "FAIL");
            break;

        case SC_EXIT_CODE_ZERO:
            ok = (exit_code == 0);
            strbuf_appendf(&sb, "exit_zero(code=%d)=%s; ", exit_code, ok ? "pass" : "FAIL");
            break;

        case SC_FILE_EXISTS: {
            struct stat st;
            char path[NC_PATH_MAX];
            if (workdir && workdir[0])
                snprintf(path, sizeof(path), "%s/%s", workdir, r->path);
            else
                nc_strlcpy(path, r->path, sizeof(path));
            ok = (stat(path, &st) == 0);
            strbuf_appendf(&sb, "file_exists(%s)=%s; ", r->path, ok ? "pass" : "FAIL");
            break;
        }

        case SC_FILE_NONEMPTY: {
            char *content = _read_file(workdir, r->path);
            ok = (content != NULL && content[0] != '\0');
            free(content);
            strbuf_appendf(&sb, "file_nonempty(%s)=%s; ", r->path, ok ? "pass" : "FAIL");
            break;
        }

        case SC_FILE_CONTAINS: {
            char *content = _read_file(workdir, r->path);
            if (content) {
                ok = r->case_sensitive ? (strstr(content, r->value) != NULL)
                                       : nc_contains_ci(content, r->value);
                free(content);
            }
            strbuf_appendf(&sb, "file_contains(%s,'%s')=%s; ", r->path, r->value,
                           ok ? "pass" : "FAIL");
            break;
        }
        }
        if (!ok)
            all_pass = false;
    }

    if (detail && sb.data)
        nc_strlcpy(detail, sb.data, detail_sz);
    strbuf_free(&sb);
    return all_pass;
}

const char *sc_kind_name(sc_kind_t k) {
    switch (k) {
    case SC_OUTPUT_CONTAINS:
        return "output_contains";
    case SC_OUTPUT_NOT_CONTAINS:
        return "output_not_contains";
    case SC_FILE_EXISTS:
        return "file_exists";
    case SC_FILE_CONTAINS:
        return "file_contains";
    case SC_FILE_NONEMPTY:
        return "file_nonempty";
    case SC_EXIT_CODE_ZERO:
        return "exit_zero";
    }
    return "unknown";
}
