/*
 * git_ops.c — Git command execution via fork/exec
 *
 * Safe subprocess-based git runner that captures stdout/stderr
 * without invoking the shell.  Used by ghost snapshots, undo,
 * commit attribution, and other git-dependent subsystems.
 */

#include "nevercode.h"
#include "defaults.h"
#include "git_ops.h"

static int run_git(const char *cwd, const char *const *args, int argc, char *out, int out_cap) {
    int pipefd[2];
    if (nc_pipe_cloexec(pipefd) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        if (cwd && chdir(cwd) != 0)
            _exit(127);

        const char *argv[GIT_OPS_MAX_ARGS + 2] = {"git"};
        int n = 1;
        for (int i = 0; i < argc && n < GIT_OPS_MAX_ARGS + 1; i++)
            argv[n++] = args[i];
        argv[n] = NULL;

        execvp("git", (char *const *)argv);
        _exit(127);
    }

    close(pipefd[1]);
    int total = 0;
    char discard[NC_IO_BUF];
    while (1) {
        char *dest = discard;
        int cap = (int)sizeof(discard);
        if (out_cap > 1 && total < out_cap - 1) {
            dest = out + total;
            cap = out_cap - 1 - total;
        }
        int r = (int)read(pipefd[0], dest, cap);
        if (r <= 0)
            break;
        if (dest != discard)
            total += r;
    }
    if (out_cap > 0)
        out[total] = '\0';
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    while (total > 0 && (out[total - 1] == '\n' || out[total - 1] == '\r'))
        out[--total] = '\0';

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int run_git_count_lines(const char *cwd, const char *const *args, int argc) {
    int pipefd[2];
    if (nc_pipe_cloexec(pipefd) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        if (cwd && chdir(cwd) != 0)
            _exit(127);

        const char *argv[GIT_OPS_MAX_ARGS + 2] = {"git"};
        int n = 1;
        for (int i = 0; i < argc && n < GIT_OPS_MAX_ARGS + 1; i++)
            argv[n++] = args[i];
        argv[n] = NULL;

        execvp("git", (char *const *)argv);
        _exit(127);
    }

    close(pipefd[1]);

    int lines = 0;
    bool saw_any = false;
    char last = '\0';
    char buf[NC_IO_BUF];
    ssize_t nread;
    while ((nread = read(pipefd[0], buf, sizeof(buf))) > 0) {
        saw_any = true;
        for (ssize_t i = 0; i < nread; i++) {
            if (buf[i] == '\n')
                lines++;
            last = buf[i];
        }
    }
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0))
        return -1;

    if (saw_any && last != '\n')
        lines++;
    return lines;
}

bool git_is_repo(const char *path) {
    char buf[NC_NUM_MAX];
    const char *args[] = {"rev-parse", "--is-inside-work-tree"};
    return run_git(path, args, 2, buf, sizeof(buf)) == 0 && nc_starts_with(buf, "true");
}

int git_repo_root(const char *path, char *out, int cap) {
    const char *args[] = {"rev-parse", "--show-toplevel"};
    return run_git(path, args, 2, out, cap);
}

int git_current_branch(const char *path, char *out, int cap) {
    const char *args[] = {"rev-parse", "--abbrev-ref", "HEAD"};
    return run_git(path, args, 3, out, cap);
}

int git_head_sha(const char *path, char *out, int cap) {
    const char *args[] = {"rev-parse", "HEAD"};
    return run_git(path, args, 2, out, cap);
}

int git_log_oneline(const char *path, int count, char *out, int cap) {
    char count_str[NC_NUM_MAX];
    snprintf(count_str, sizeof(count_str), "-%d", count > 0 ? count : 5);
    const char *args[] = {"log", "--oneline", count_str};
    return run_git(path, args, 3, out, cap);
}

int git_dirty_count(const char *path) {
    const char *args[] = {"status", "--porcelain"};
    return run_git_count_lines(path, args, 2);
}

int git_tracked_file_count(const char *path) {
    const char *args[] = {"ls-files"};
    return run_git_count_lines(path, args, 1);
}

bool git_has_uncommitted(const char *path) {
    char buf[NC_MSG_MAX];
    const char *args[] = {"status", "--porcelain"};
    return run_git(path, args, 2, buf, sizeof(buf)) == 0 && buf[0];
}

int git_add_all(const char *path) {
    char buf[NC_FMT_MAX];
    const char *args[] = {"add", "--all"};
    return run_git(path, args, 2, buf, sizeof(buf));
}

int git_write_tree(const char *path, char *out, int cap) {
    const char *args[] = {"write-tree"};
    return run_git(path, args, 1, out, cap);
}

int git_commit(const char *path, const char *message) {
    char buf[NC_LABEL_MAX];
    const char *args[] = {"commit", "-m", message};
    return run_git(path, args, 3, buf, sizeof(buf));
}

int git_checkpoint_commit(const char *path, const char *message) {
    if (git_add_all(path) != 0)
        return -1;
    char buf[NC_MSG_MAX];
    const char *args[] = {"commit", "--quiet", "--no-gpg-sign", "-m", message};
    return run_git(path, args, 5, buf, sizeof(buf));
}

int git_commit_tree(const char *path, const char *tree, const char *message, char *out, int cap) {
    const char *args[] = {"commit-tree", tree, "-m", message};
    return run_git(path, args, 4, out, cap);
}

int git_ref_set(const char *path, const char *ref, const char *value) {
    char buf[NC_FMT_MAX];
    const char *args[] = {"update-ref", ref, value};
    return run_git(path, args, 3, buf, sizeof(buf));
}

int git_ref_delete(const char *path, const char *ref) {
    char buf[NC_FMT_MAX];
    const char *args[] = {"update-ref", "-d", ref};
    return run_git(path, args, 3, buf, sizeof(buf));
}

int git_ref_list(const char *path, const char *prefix, char *out, int cap) {
    const char *args[] = {"for-each-ref", "--format=%(refname)", prefix};
    return run_git(path, args, 3, out, cap);
}

int git_read_tree(const char *path, const char *ref) {
    char buf[NC_FMT_MAX];
    const char *args[] = {"read-tree", ref};
    return run_git(path, args, 2, buf, sizeof(buf));
}

int git_checkout_index_all(const char *path) {
    char buf[NC_FMT_MAX];
    const char *args[] = {"checkout-index", "-a", "-f"};
    return run_git(path, args, 3, buf, sizeof(buf));
}

int git_stash_push(const char *path, const char *message, bool include_untracked, char *out,
                   int cap) {
    if (include_untracked) {
        const char *args[] = {"stash", "push", "-u", "-m", message};
        return run_git(path, args, 5, out, cap);
    }
    const char *args[] = {"stash", "push", "-m", message};
    return run_git(path, args, 4, out, cap);
}

int git_stash_list(const char *path, char *out, int cap) {
    const char *args[] = {"stash", "list"};
    return run_git(path, args, 2, out, cap);
}

int git_stash_list_refs(const char *path, char *out, int cap) {
    const char *args[] = {"stash", "list", "--format=%gd"};
    return run_git(path, args, 3, out, cap);
}

int git_stash_list_hashes(const char *path, char *out, int cap) {
    const char *args[] = {"stash", "list", "--format=%H"};
    return run_git(path, args, 3, out, cap);
}

int git_stash_apply(const char *path, const char *stash_ref) {
    char buf[NC_MSG_MAX];
    const char *args[] = {"stash", "apply", "--quiet", stash_ref};
    return run_git(path, args, 4, buf, sizeof(buf));
}

static int git_reset_hard_head(const char *path) {
    char buf[NC_MSG_MAX];
    const char *args[] = {"reset", "--hard", "--quiet", "HEAD"};
    return run_git(path, args, 4, buf, sizeof(buf));
}

static int git_clean_untracked(const char *path) {
    char buf[NC_MSG_MAX];
    const char *args[] = {"clean", "-fd", "--quiet"};
    return run_git(path, args, 3, buf, sizeof(buf));
}

int git_stash_restore_exact(const char *path, const char *stash_ref) {
    if (!stash_ref || !stash_ref[0])
        return -1;
    if (git_reset_hard_head(path) != 0)
        return -1;
    if (git_clean_untracked(path) != 0)
        return -1;
    return git_stash_apply(path, stash_ref);
}

int git_tag_create(const char *path, const char *tag) {
    char buf[NC_LABEL_MAX];
    const char *args[] = {"tag", tag};
    return run_git(path, args, 2, buf, sizeof(buf));
}

int git_tag_delete(const char *path, const char *tag) {
    char buf[NC_LABEL_MAX];
    const char *args[] = {"tag", "-d", tag};
    return run_git(path, args, 3, buf, sizeof(buf));
}

int git_restore_all_from_ref(const char *path, const char *ref) {
    char buf[NC_MSG_MAX];
    const char *args[] = {"checkout", ref, "--", "."};
    return run_git(path, args, 4, buf, sizeof(buf));
}

int git_push(const char *path) {
    char buf[NC_MSG_MAX];
    const char *args[] = {"push"};
    return run_git(path, args, 1, buf, sizeof(buf));
}

int git_remote_url(const char *path, char *out, int cap) {
    const char *args[] = {"remote", "get-url", "origin"};
    return run_git(path, args, 3, out, cap);
}

int git_diff_stat(const char *path, char *out, int cap) {
    const char *args[] = {"diff", "--stat", "HEAD"};
    return run_git(path, args, 3, out, cap);
}

int git_diff_output(const char *path, char *out, int cap) {
    const char *args[] = {"diff", "HEAD"};
    return run_git(path, args, 2, out, cap);
}

int git_status_short(const char *path, char *out, int cap) {
    const char *args[] = {"status", "--short"};
    return run_git(path, args, 2, out, cap);
}

int git_diff_ex(const char *path, bool staged, const char *target, char *out, int cap) {
    const char *argv[GIT_OPS_MAX_ARGS];
    int n = 0;
    argv[n++] = "diff";
    if (staged)
        argv[n++] = "--cached";
    if (target) {
        argv[n++] = "--";
        argv[n++] = target;
    }
    return run_git(path, argv, n, out, cap);
}

int git_diff_stat_ex(const char *path, bool staged, const char *target, char *out, int cap) {
    const char *argv[GIT_OPS_MAX_ARGS];
    int n = 0;
    argv[n++] = "diff";
    if (staged)
        argv[n++] = "--cached";
    argv[n++] = "--stat";
    if (target) {
        argv[n++] = "--";
        argv[n++] = target;
    }
    return run_git(path, argv, n, out, cap);
}

int git_ls_untracked(const char *path, char *out, int cap) {
    const char *args[] = {"ls-files", "--others", "--exclude-standard"};
    return run_git(path, args, 3, out, cap);
}

int git_log_format(const char *path, int count, const char *fmt, char *out, int cap) {
    char count_str[NC_NUM_MAX];
    snprintf(count_str, sizeof(count_str), "-%d", count > 0 ? count : 1);
    char fmt_str[NC_TAG_MAX];
    snprintf(fmt_str, sizeof(fmt_str), "--format=%s", fmt);
    const char *args[] = {"log", count_str, fmt_str};
    return run_git(path, args, 3, out, cap);
}

int git_worktree_add(const char *path, const char *dir, const char *branch, char *out, int cap) {
    const char *args[] = {"worktree", "add", "-b", branch, dir, "HEAD"};
    int rc = run_git(path, args, 6, out, cap);
    if (rc != 0) {
        const char *fallback[] = {"worktree", "add", dir, branch};
        rc = run_git(path, fallback, 4, out, cap);
    }
    return rc;
}

int git_worktree_remove(const char *path, const char *dir, char *out, int cap) {
    const char *args[] = {"worktree", "remove", "--force", dir};
    return run_git(path, args, 4, out, cap);
}

char *git_cmd_alloc(const char *path, const char *const *args, int argc, int max_bytes) {
    int pipefd[2];
    if (nc_pipe_cloexec(pipefd) < 0)
        return NULL;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        if (path && chdir(path) != 0)
            _exit(127);

        const char *argv[GIT_OPS_MAX_ARGS + 2] = {"git"};
        int n = 1;
        for (int i = 0; i < argc && n < GIT_OPS_MAX_ARGS + 1; i++)
            argv[n++] = args[i];
        argv[n] = NULL;

        execvp("git", (char *const *)argv);
        _exit(127);
    }

    close(pipefd[1]);

    int cap = max_bytes > 0 ? max_bytes : GIT_CMD_DEFAULT_CAP;
    char *buf = malloc(cap + 1);
    if (!buf) {
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return NULL;
    }

    int total = 0;
    char discard[NC_IO_BUF];
    while (1) {
        char *dest = (total < cap) ? buf + total : discard;
        int rd_cap = (total < cap) ? cap - total : (int)sizeof(discard);
        int r = (int)read(pipefd[0], dest, rd_cap);
        if (r <= 0)
            break;
        if (dest != discard)
            total += r;
    }
    buf[total] = '\0';
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        free(buf);
        return NULL;
    }

    while (total > 0 && (buf[total - 1] == '\n' || buf[total - 1] == '\r'))
        buf[--total] = '\0';

    return buf;
}
