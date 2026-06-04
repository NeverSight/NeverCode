/*
 * cmd_canonical.c — Command canonicalization for stable approval-cache matching
 *
 * Normalizes bash command argv so approval decisions remain stable across
 * wrapper-path differences (e.g. /bin/bash -lc vs bash -lc) and across
 * shell wrappers.
 */

#include "nevercode.h"

#define CANONICAL_SHELL_SCRIPT "__nevercode_shell_script__"

static bool is_shell_binary(const char *name) {
    if (!name)
        return false;
    const char *base = strrchr(name, '/');
    base = base ? base + 1 : name;
    return (strcmp(base, "bash") == 0 || strcmp(base, "sh") == 0 || strcmp(base, "zsh") == 0 ||
            strcmp(base, "dash") == 0 || strcmp(base, "fish") == 0);
}

/*
 * Extract the inner command from "shell [-flags] -c 'command'" patterns.
 * Returns the script string if found, NULL otherwise.
 */
static const char *extract_shell_c_script(const char *cmd) {
    if (!cmd)
        return NULL;

    while (*cmd && isspace((unsigned char)*cmd))
        cmd++;

    const char *rest = cmd;
    while (*rest && !isspace((unsigned char)*rest))
        rest++;

    char shell[NC_LABEL_MAX];
    size_t slen = (size_t)(rest - cmd);
    if (slen >= sizeof(shell))
        return NULL;
    memcpy(shell, cmd, slen);
    shell[slen] = '\0';

    if (!is_shell_binary(shell))
        return NULL;

    while (*rest && isspace((unsigned char)*rest))
        rest++;

    bool found_c = false;
    while (*rest == '-') {
        const char *flag_start = rest;
        rest++;
        while (*rest && !isspace((unsigned char)*rest)) {
            if (*rest == 'c')
                found_c = true;
            rest++;
        }
        (void)flag_start;
        while (*rest && isspace((unsigned char)*rest))
            rest++;
    }

    if (!found_c)
        return NULL;

    while (*rest && isspace((unsigned char)*rest))
        rest++;
    if (*rest == '\0')
        return NULL;

    if (*rest == '\'' || *rest == '"')
        rest++;

    return rest;
}

char *cmd_canonicalize(const char *cmd) {
    if (!cmd || !*cmd)
        return NULL;

    while (*cmd && isspace((unsigned char)*cmd))
        cmd++;

    const char *script = extract_shell_c_script(cmd);
    if (script) {
        size_t slen = strlen(script);
        if (slen > 0 && (script[slen - 1] == '\'' || script[slen - 1] == '"'))
            slen--;

        size_t prefix_len = strlen(CANONICAL_SHELL_SCRIPT);
        char *out = malloc(prefix_len + 1 + slen + 1);
        if (!out)
            return NULL;
        memcpy(out, CANONICAL_SHELL_SCRIPT, prefix_len);
        out[prefix_len] = ' ';
        memcpy(out + prefix_len + 1, script, slen);
        out[prefix_len + 1 + slen] = '\0';
        return out;
    }

    return nc_xstrdup(cmd);
}

bool cmd_canonical_equal(const char *a, const char *b) {
    if (!a || !b)
        return false;
    char *ca = cmd_canonicalize(a);
    char *cb = cmd_canonicalize(b);
    bool eq = (ca && cb && strcmp(ca, cb) == 0);
    free(ca);
    free(cb);
    return eq;
}

/*
 * Approval cache: remembers canonicalized commands that were approved.
 */
static char *approval_cache[MAX_APPROVAL_CACHE];
static int approval_cache_count = 0;

void approval_cache_init(void) {
    approval_cache_count = 0;
    memset(approval_cache, 0, sizeof(approval_cache));
}

void approval_cache_free(void) {
    for (int i = 0; i < approval_cache_count; i++) {
        free(approval_cache[i]);
        approval_cache[i] = NULL;
    }
    approval_cache_count = 0;
}

void approval_cache_add(const char *cmd) {
    if (!cmd || approval_cache_count >= MAX_APPROVAL_CACHE)
        return;
    char *canon = cmd_canonicalize(cmd);
    if (!canon)
        return;

    for (int i = 0; i < approval_cache_count; i++) {
        if (strcmp(approval_cache[i], canon) == 0) {
            free(canon);
            return;
        }
    }
    approval_cache[approval_cache_count++] = canon;
}

bool approval_cache_check(const char *cmd) {
    if (!cmd)
        return false;
    char *canon = cmd_canonicalize(cmd);
    if (!canon)
        return false;

    for (int i = 0; i < approval_cache_count; i++) {
        if (strcmp(approval_cache[i], canon) == 0) {
            free(canon);
            return true;
        }
    }
    free(canon);
    return false;
}

int approval_cache_count_entries(void) {
    return approval_cache_count;
}
