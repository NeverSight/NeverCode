/*
 * review_prompt.c — Template-based review prompt generation
 *
 * Generates structured review prompts with variable substitution
 * for different review targets (uncommitted, branch diff, commit).
 */

#include "nevercode.h"

/* RP_VAR_MAX, RP_VAR_KEY_MAX, RP_VAR_VAL_MAX in defaults.h */
#define RP_TEMPLATE_MAX NC_PATH_MAX

typedef struct {
    char key[RP_VAR_KEY_MAX];
    char value[RP_VAR_VAL_MAX];
} rp_var_t;

typedef enum {
    RP_TARGET_UNCOMMITTED,
    RP_TARGET_BRANCH,
    RP_TARGET_COMMIT,
    RP_TARGET_FILE,
    RP_TARGET_CUSTOM
} rp_target_t;

static const char *RP_UNCOMMITTED =
    "Review the current code changes (staged, unstaged, and untracked files) "
    "and provide prioritized findings.";

static const char *RP_BRANCH_WITH_BASE =
    "Review the code changes against the base branch '{{base_branch}}'. "
    "The merge base commit is {{merge_base}}. Run `git diff {{merge_base}}` "
    "to inspect the changes. Provide prioritized, actionable findings.";

static const char *RP_BRANCH_FALLBACK =
    "Review the code changes against the base branch '{{branch}}'. "
    "Start by finding the merge base (`git merge-base HEAD {{branch}}`), "
    "then diff against that SHA. Provide prioritized, actionable findings.";

static const char *RP_COMMIT_WITH_TITLE =
    "Review the code changes introduced by commit {{sha}} (\"{{title}}\"). "
    "Provide prioritized, actionable findings.";

static const char *RP_COMMIT = "Review the code changes introduced by commit {{sha}}. "
                               "Provide prioritized, actionable findings.";

static const char *RP_FILE = "Review the file {{path}} for issues. "
                             "Provide prioritized, actionable findings.";

static char *render_template(const char *tpl, const rp_var_t *vars, int nvars) {
    strbuf_t sb;
    strbuf_init(&sb);
    const char *p = tpl;

    while (*p) {
        if (p[0] == '{' && p[1] == '{') {
            const char *end = strstr(p + 2, "}}");
            if (end) {
                size_t klen = (size_t)(end - p - 2);
                char key[RP_VAR_KEY_MAX];
                if (klen >= sizeof(key))
                    klen = sizeof(key) - 1;
                memcpy(key, p + 2, klen);
                key[klen] = '\0';

                bool found = false;
                for (int i = 0; i < nvars; i++) {
                    if (strcmp(vars[i].key, key) == 0) {
                        strbuf_append_str(&sb, vars[i].value);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    strbuf_append(&sb, p, (size_t)(end + 2 - p));
                }
                p = end + 2;
                continue;
            }
        }
        strbuf_append(&sb, p, 1);
        p++;
    }
    return strbuf_detach(&sb);
}

char *rp_build_uncommitted(void) {
    return nc_xstrdup(RP_UNCOMMITTED);
}

char *rp_build_branch(const char *branch, const char *merge_base) {
    rp_var_t vars[2];
    int nv = 0;

    if (merge_base && merge_base[0]) {
        snprintf(vars[0].key, sizeof(vars[0].key), "base_branch");
        nc_strlcpy(vars[0].value, branch, sizeof(vars[0].value));
        snprintf(vars[1].key, sizeof(vars[1].key), "merge_base");
        nc_strlcpy(vars[1].value, merge_base, sizeof(vars[1].value));
        nv = 2;
        return render_template(RP_BRANCH_WITH_BASE, vars, nv);
    }

    snprintf(vars[0].key, sizeof(vars[0].key), "branch");
    nc_strlcpy(vars[0].value, branch ? branch : "main", sizeof(vars[0].value));
    nv = 1;
    return render_template(RP_BRANCH_FALLBACK, vars, nv);
}

char *rp_build_commit(const char *sha, const char *title) {
    rp_var_t vars[2];
    int nv = 0;

    snprintf(vars[0].key, sizeof(vars[0].key), "sha");
    nc_strlcpy(vars[0].value, sha ? sha : "HEAD", sizeof(vars[0].value));
    nv = 1;

    if (title && title[0]) {
        snprintf(vars[1].key, sizeof(vars[1].key), "title");
        nc_strlcpy(vars[1].value, title, sizeof(vars[1].value));
        nv = 2;
        return render_template(RP_COMMIT_WITH_TITLE, vars, nv);
    }
    return render_template(RP_COMMIT, vars, nv);
}

char *rp_build_file(const char *path) {
    rp_var_t vars[1];
    snprintf(vars[0].key, sizeof(vars[0].key), "path");
    nc_strlcpy(vars[0].value, path ? path : "(unknown)", sizeof(vars[0].value));
    return render_template(RP_FILE, vars, 1);
}

char *rp_build_custom(const char *template, const char **keys, const char **values, int count) {
    if (!template)
        return NULL;

    rp_var_t vars[RP_VAR_MAX];
    int nv = count > RP_VAR_MAX ? RP_VAR_MAX : count;
    for (int i = 0; i < nv; i++) {
        nc_strlcpy(vars[i].key, keys[i] ? keys[i] : "", sizeof(vars[i].key));
        nc_strlcpy(vars[i].value, values[i] ? values[i] : "", sizeof(vars[i].value));
    }
    return render_template(template, vars, nv);
}

const char *rp_target_name(rp_target_t t) {
    switch (t) {
    case RP_TARGET_UNCOMMITTED:
        return "uncommitted";
    case RP_TARGET_BRANCH:
        return "branch";
    case RP_TARGET_COMMIT:
        return "commit";
    case RP_TARGET_FILE:
        return "file";
    case RP_TARGET_CUSTOM:
        return "custom";
    default:
        return "unknown";
    }
}
