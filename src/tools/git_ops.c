/*
 * git_ops.c — Classify git operations from bash command strings
 *
 * Detects branch, commit, PR, and other git operations inside
 * bash commands so the harness / UI can display smart summaries
 * and track repository-level state transitions.
 */

#include "nevercode.h"

/* ── Helpers ── */

/* Find "git " or "git\t" at word boundary in cmd.  Returns pointer past "git ". */
static const char *find_git_cmd(const char *cmd) {
    const char *p = cmd;
    while ((p = strstr(p, "git")) != NULL) {
        if (p != cmd && !isspace((unsigned char)p[-1]) && p[-1] != '&' && p[-1] != '|' &&
            p[-1] != ';') {
            p++;
            continue;
        }
        const char *after = p + 3;
        if (*after == ' ' || *after == '\t')
            return nc_skip_ws(after);
        p++;
    }
    return NULL;
}

/* Extract next word token (NUL-terminated copy). Caller frees. */
static char *next_word(const char *s) {
    s = nc_skip_ws(s);
    if (!*s)
        return NULL;
    const char *e = s;
    while (*e && !isspace((unsigned char)*e))
        e++;
    size_t len = e - s;
    char *w = malloc(len + 1);
    if (w) {
        memcpy(w, s, len);
        w[len] = '\0';
    }
    return w;
}

/* ── Core classifier ── */

git_op_t git_ops_classify(const char *cmd) {
    git_op_t op = {GIT_OP_NONE, false, NULL};
    if (!cmd)
        return op;

    /* Also check for gh CLI PR operations. */
    const char *gh = strstr(cmd, "gh ");
    if (gh) {
        if (strstr(gh, "pr create"))
            op.kind = GIT_OP_PR_CREATE;
        else if (strstr(gh, "pr merge"))
            op.kind = GIT_OP_PR_MERGE;
        if (op.kind != GIT_OP_NONE)
            return op;
    }

    const char *sub = find_git_cmd(cmd);
    if (!sub)
        return op;

    char *verb = next_word(sub);
    if (!verb)
        return op;

    if (strcmp(verb, "commit") == 0) {
        op.kind = GIT_OP_COMMIT;
    } else if (strcmp(verb, "push") == 0) {
        op.kind = GIT_OP_PUSH;
        if (strstr(sub, "--force") || strstr(sub, "-f"))
            op.destructive = true;
    } else if (strcmp(verb, "pull") == 0) {
        op.kind = GIT_OP_PULL;
    } else if (strcmp(verb, "checkout") == 0) {
        if (strstr(sub, "-b"))
            op.kind = GIT_OP_BRANCH_CREATE;
        else
            op.kind = GIT_OP_BRANCH_SWITCH;
    } else if (strcmp(verb, "switch") == 0) {
        if (strstr(sub, "-c") || strstr(sub, "--create"))
            op.kind = GIT_OP_BRANCH_CREATE;
        else
            op.kind = GIT_OP_BRANCH_SWITCH;
    } else if (strcmp(verb, "branch") == 0) {
        if (strstr(sub, "-d") || strstr(sub, "-D") || strstr(sub, "--delete")) {
            op.kind = GIT_OP_BRANCH_DELETE;
            op.destructive = true;
        } else {
            op.kind = GIT_OP_BRANCH_CREATE;
        }
    } else if (strcmp(verb, "merge") == 0) {
        op.kind = GIT_OP_MERGE;
    } else if (strcmp(verb, "rebase") == 0) {
        op.kind = GIT_OP_REBASE;
        op.destructive = true;
    } else if (strcmp(verb, "stash") == 0) {
        op.kind = GIT_OP_STASH;
    } else if (strcmp(verb, "tag") == 0) {
        op.kind = GIT_OP_TAG;
    } else if (strcmp(verb, "reset") == 0) {
        op.kind = GIT_OP_RESET;
        if (strstr(sub, "--hard"))
            op.destructive = true;
    } else if (strcmp(verb, "cherry-pick") == 0) {
        op.kind = GIT_OP_CHERRY_PICK;
    } else if (strcmp(verb, "status") == 0) {
        op.kind = GIT_OP_STATUS;
    } else if (strcmp(verb, "diff") == 0) {
        op.kind = GIT_OP_DIFF;
    } else if (strcmp(verb, "log") == 0) {
        op.kind = GIT_OP_LOG;
    } else if (strcmp(verb, "add") == 0) {
        op.kind = GIT_OP_ADD;
    } else if (strcmp(verb, "clone") == 0) {
        op.kind = GIT_OP_CLONE;
    } else if (strcmp(verb, "fetch") == 0) {
        op.kind = GIT_OP_FETCH;
    } else if (strcmp(verb, "init") == 0) {
        op.kind = GIT_OP_INIT;
    } else if (strcmp(verb, "submodule") == 0) {
        op.kind = GIT_OP_SUBMODULE;
    } else if (strcmp(verb, "worktree") == 0) {
        op.kind = GIT_OP_WORKTREE;
    }

    free(verb);
    return op;
}

const char *git_op_name(git_op_kind_t kind) {
    switch (kind) {
    case GIT_OP_NONE:
        return "none";
    case GIT_OP_COMMIT:
        return "commit";
    case GIT_OP_PUSH:
        return "push";
    case GIT_OP_PULL:
        return "pull";
    case GIT_OP_BRANCH_CREATE:
        return "branch-create";
    case GIT_OP_BRANCH_SWITCH:
        return "branch-switch";
    case GIT_OP_BRANCH_DELETE:
        return "branch-delete";
    case GIT_OP_MERGE:
        return "merge";
    case GIT_OP_REBASE:
        return "rebase";
    case GIT_OP_STASH:
        return "stash";
    case GIT_OP_TAG:
        return "tag";
    case GIT_OP_RESET:
        return "reset";
    case GIT_OP_CHERRY_PICK:
        return "cherry-pick";
    case GIT_OP_PR_CREATE:
        return "pr-create";
    case GIT_OP_PR_MERGE:
        return "pr-merge";
    case GIT_OP_STATUS:
        return "status";
    case GIT_OP_DIFF:
        return "diff";
    case GIT_OP_LOG:
        return "log";
    case GIT_OP_ADD:
        return "add";
    case GIT_OP_CLONE:
        return "clone";
    case GIT_OP_FETCH:
        return "fetch";
    case GIT_OP_INIT:
        return "init";
    case GIT_OP_SUBMODULE:
        return "submodule";
    case GIT_OP_WORKTREE:
        return "worktree";
    }
    return "unknown";
}

bool git_op_is_mutating(git_op_kind_t kind) {
    switch (kind) {
    case GIT_OP_COMMIT:
    case GIT_OP_PUSH:
    case GIT_OP_PULL:
    case GIT_OP_BRANCH_CREATE:
    case GIT_OP_BRANCH_DELETE:
    case GIT_OP_MERGE:
    case GIT_OP_REBASE:
    case GIT_OP_STASH:
    case GIT_OP_TAG:
    case GIT_OP_RESET:
    case GIT_OP_CHERRY_PICK:
    case GIT_OP_PR_CREATE:
    case GIT_OP_PR_MERGE:
    case GIT_OP_ADD:
    case GIT_OP_CLONE:
    case GIT_OP_INIT:
        return true;
    default:
        return false;
    }
}

/* Cumulative session tracking. */
static struct {
    int counts[GIT_OPS_KIND_MAX];
    int destructive_count;
} g_git_ops;

void git_ops_record(git_op_kind_t kind, bool destructive) {
    if (kind <= GIT_OP_NONE || kind >= GIT_OPS_KIND_MAX)
        return;
    g_git_ops.counts[kind]++;
    if (destructive)
        g_git_ops.destructive_count++;
}

int git_ops_session_count(git_op_kind_t kind) {
    if (kind <= GIT_OP_NONE || kind >= GIT_OPS_KIND_MAX)
        return 0;
    return g_git_ops.counts[kind];
}

int git_ops_total_destructive(void) {
    return g_git_ops.destructive_count;
}

char *git_ops_format_summary(void) {
    int total = 0;
    for (int i = 1; i < GIT_OPS_KIND_MAX; i++)
        total += g_git_ops.counts[i];
    if (total == 0)
        return nc_xstrdup("(no git operations)");

    char buf[NC_LINE_MAX];
    int off = 0;
    off += snprintf(buf + off, sizeof(buf) - off, "Git ops (%d total):", total);
    for (int i = 1; i < GIT_OPS_KIND_MAX; i++) {
        if (g_git_ops.counts[i] > 0)
            off += snprintf(buf + off, sizeof(buf) - off, " %s=%d", git_op_name((git_op_kind_t)i),
                            g_git_ops.counts[i]);
    }
    if (g_git_ops.destructive_count > 0)
        off += snprintf(buf + off, sizeof(buf) - off, " [%d destructive]",
                        g_git_ops.destructive_count);
    return nc_xstrdup(buf);
}

void git_ops_reset(void) {
    memset(&g_git_ops, 0, sizeof(g_git_ops));
}
