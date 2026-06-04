/*
 * exec_rules.c — Execution policy rules system
 *
 * Loads .rules files from config directories.
 * Each rule is a prefix-match pattern with allow/deny/prompt decision.
 */

#include "nevercode.h"
#include "defaults.h"
#include "permission.h"
#include <fnmatch.h>

static exec_rules_ctx_t g_exec_rules;

void exec_rules_init(exec_rules_ctx_t *r) {
    memset(r, 0, sizeof(*r));
    r->default_decision = EXEC_ALLOW;
}

void exec_rules_free(exec_rules_ctx_t *r) {
    for (int i = 0; i < r->num_rules; i++) {
        free(r->rules[i].prefix);
        free(r->rules[i].justification);
    }
    r->num_rules = 0;
}

int exec_rules_add(exec_rules_ctx_t *r, const char *prefix, exec_decision_t decision,
                   const char *justification) {
    if (!r || !prefix || r->num_rules >= MAX_EXEC_RULES)
        return -1;

    exec_rule_t *rule = &r->rules[r->num_rules];
    rule->prefix = nc_xstrdup(prefix);
    rule->decision = decision;
    rule->justification = justification ? nc_xstrdup(justification) : NULL;
    r->num_rules++;
    return 0;
}

/*
 * Parse a single .rules file. Format:
 *   # comment
 *   allow <prefix>
 *   deny <prefix> # justification
 *   prompt <prefix> # justification
 *
 * Lines starting with # are comments. Inline # after command is justification.
 */
int exec_rules_parse(exec_rules_ctx_t *r, const char *content) {
    if (!r || !content)
        return -1;

    const char *line = content;
    int count = 0;
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);

        char buf[NC_LINE_MAX];
        if (len >= sizeof(buf))
            len = sizeof(buf) - 1;
        memcpy(buf, line, len);
        buf[len] = '\0';

        char *p = buf;
        while (*p && isspace((unsigned char)*p))
            p++;

        if (*p && *p != '#') {
            exec_decision_t decision = EXEC_ALLOW;
            char *cmd_start = NULL;
            char *justification = NULL;

            if (strncasecmp(p, "allow ", 6) == 0) {
                decision = EXEC_ALLOW;
                cmd_start = p + 6;
            } else if (strncasecmp(p, "deny ", 5) == 0) {
                decision = EXEC_DENY;
                cmd_start = p + 5;
            } else if (strncasecmp(p, "forbidden ", 10) == 0) {
                decision = EXEC_DENY;
                cmd_start = p + 10;
            } else if (strncasecmp(p, "prompt ", 7) == 0) {
                decision = EXEC_PROMPT;
                cmd_start = p + 7;
            }

            if (cmd_start) {
                while (*cmd_start && isspace((unsigned char)*cmd_start))
                    cmd_start++;

                char *hash = strchr(cmd_start, '#');
                if (hash) {
                    *hash = '\0';
                    justification = hash + 1;
                    while (*justification && isspace((unsigned char)*justification))
                        justification++;
                    char *jend = justification + strlen(justification) - 1;
                    while (jend > justification && isspace((unsigned char)*jend))
                        *jend-- = '\0';
                }

                char *cend = cmd_start + strlen(cmd_start) - 1;
                while (cend > cmd_start && isspace((unsigned char)*cend))
                    *cend-- = '\0';

                if (*cmd_start) {
                    exec_rules_add(r, cmd_start, decision,
                                   (justification && *justification) ? justification : NULL);
                    count++;
                }
            }
        }

        line = end ? end + 1 : line + len;
    }
    return count;
}

int exec_rules_load_file(exec_rules_ctx_t *r, const char *path) {
    char *content = strbuf_read_file_max(path, NULL, NC_READ_CAP_JSON_MANIFEST);
    if (!content)
        return -1;

    int n = exec_rules_parse(r, content);
    free(content);
    return n;
}

int exec_rules_load_dir(exec_rules_ctx_t *r, const char *dir) {
    DIR *d = opendir(dir);
    if (!d)
        return 0;

    int total = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (str_has_suffix(ent->d_name, ".rules")) {
            char path[NC_LINE_MAX];
            snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
            int n = exec_rules_load_file(r, path);
            if (n > 0)
                total += n;
        }
    }
    closedir(d);
    return total;
}

/*
 * Check command against rules. Last matching rule wins (highest specificity).
 * Rules with longer prefix match take precedence.
 */
exec_decision_t exec_rules_check(const exec_rules_ctx_t *r, const char *command,
                                 const char **justification_out) {
    if (!r || !command)
        return EXEC_ALLOW;

    exec_decision_t best_decision = r->default_decision;
    int best_match_len = -1;
    const char *best_justification = NULL;

    for (int i = 0; i < r->num_rules; i++) {
        const exec_rule_t *rule = &r->rules[i];
        size_t prefix_len = strlen(rule->prefix);

        bool matches = false;

        if (strchr(rule->prefix, '*') || strchr(rule->prefix, '?')) {
            matches = (fnmatch(rule->prefix, command, 0) == 0);
        } else {
            matches = (strncmp(command, rule->prefix, prefix_len) == 0);
        }

        if (matches && (int)prefix_len > best_match_len) {
            best_match_len = (int)prefix_len;
            best_decision = rule->decision;
            best_justification = rule->justification;
        }
    }

    if (justification_out)
        *justification_out = best_justification;
    return best_decision;
}

int exec_rules_load_from_config(exec_rules_ctx_t *r) {
    char rules_dir[NC_MSG_MAX];

    config_home_path(rules_dir, sizeof(rules_dir), "rules");
    int n1 = exec_rules_load_dir(r, rules_dir);

    static const char *cwd_dirs[] = {"." CONFIG_DIR_PRIMARY "/rules",
                                     "." CONFIG_DIR_SECONDARY "/rules",
                                     "." CONFIG_DIR_COMPAT "/rules"};
    int n_cwd = 0;
    for (int i = 0; i < NC_ARRAY_LEN(cwd_dirs); i++) {
        int n = exec_rules_load_dir(r, cwd_dirs[i]);
        if (n > 0)
            n_cwd += n;
    }
    return (n1 > 0 ? n1 : 0) + n_cwd;
}

exec_rules_ctx_t *exec_rules_global(void) {
    return &g_exec_rules;
}

char *exec_rules_format(const exec_rules_ctx_t *r) {
    if (!r || r->num_rules == 0)
        return nc_xstrdup("No exec rules loaded.\n");

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "Exec Policy Rules:\n");

    for (int i = 0; i < r->num_rules; i++) {
        const exec_rule_t *rule = &r->rules[i];
        const char *decision_str = "allow";
        if (rule->decision == EXEC_DENY)
            decision_str = "deny";
        else if (rule->decision == EXEC_PROMPT)
            decision_str = "prompt";

        char line[NC_MSG_MAX];
        if (rule->justification) {
            snprintf(line, sizeof(line), "  %s %s  # %s\n", decision_str, rule->prefix,
                     rule->justification);
        } else {
            snprintf(line, sizeof(line), "  %s %s\n", decision_str, rule->prefix);
        }
        strbuf_append_str(&sb, line);
    }
    return strbuf_detach(&sb);
}

int exec_rules_persist_allow(exec_rules_ctx_t *r, const char *prefix, const char *justification) {
    if (!r || !prefix)
        return -1;

    exec_rules_add(r, prefix, EXEC_ALLOW, justification);

    char path[NC_LINE_MAX];
    config_home_path(path, sizeof(path), "rules");
    mkdir(path, NC_DIR_MODE);

    char fpath[NC_LINE_MAX];
    snprintf(fpath, sizeof(fpath), "%s/default.rules", path);

    FILE *f = fopen(fpath, "a");
    if (!f)
        return -1;

    if (justification && *justification)
        fprintf(f, "allow %s  # %s\n", prefix, justification);
    else
        fprintf(f, "allow %s\n", prefix);

    int rc = fflush(f) ? -1 : 0;
    fclose(f);
    return rc;
}
