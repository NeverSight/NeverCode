#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include "nevercode.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * Shared test utilities used across multiple test files.
 * All functions are static inline to avoid linkage issues.
 */

static inline int th_run_cmd(const char *cwd, const char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        if (cwd && chdir(cwd) != 0)
            _exit(127);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static inline int th_remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0)
        return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode))
        return unlink(path);

    DIR *dir = opendir(path);
    if (!dir)
        return -1;
    struct dirent *ent;
    int rc = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        char child[NC_PATH_MAX];
        snprintf(child, sizeof(child), "%.2045s/%.2045s", path, ent->d_name);
        if (th_remove_tree(child) != 0)
            rc = -1;
    }
    closedir(dir);
    if (rmdir(path) != 0)
        rc = -1;
    return rc;
}

static inline int th_write_text(const char *path, const char *text) {
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return -1;
    size_t len = strlen(text);
    int rc = fwrite(text, 1, len, fp) == len ? 0 : -1;
    fclose(fp);
    return rc;
}

static inline int th_read_text(const char *path, char *out, size_t cap) {
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -1;
    size_t n = fread(out, 1, cap - 1, fp);
    out[n] = '\0';
    fclose(fp);
    return 0;
}

static inline void th_setup_git_env(void) {
    setenv("GIT_AUTHOR_NAME", "nevercode-test", 1);
    setenv("GIT_AUTHOR_EMAIL", "nevercode@example.com", 1);
    setenv("GIT_COMMITTER_NAME", "nevercode-test", 1);
    setenv("GIT_COMMITTER_EMAIL", "nevercode@example.com", 1);
    setenv("GIT_CONFIG_NOSYSTEM", "1", 1);
    setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1);
    setenv("GIT_TERMINAL_PROMPT", "0", 1);
}

static inline int th_init_temp_repo(const char *dir) {
    th_setup_git_env();
    const char *init_argv[] = {"git", "init", "-q", NULL};
    const char *add_argv[] = {"git", "add", "note.txt", NULL};
    const char *commit_argv[] = {"git", "commit", "-q", "-m", "base", NULL};
    char note_path[NC_PATH_MAX];
    snprintf(note_path, sizeof(note_path), "%s/note.txt", dir);
    if (th_run_cmd(dir, init_argv) != 0)
        return -1;
    if (th_write_text(note_path, "one\n") != 0)
        return -1;
    if (th_run_cmd(dir, add_argv) != 0)
        return -1;
    return th_run_cmd(dir, commit_argv);
}

static inline int th_enter_temp_dir(char *saved_cwd, size_t saved_cap, char *tmpdir) {
    if (!mkdtemp(tmpdir))
        return -1;
    if (!getcwd(saved_cwd, saved_cap))
        return -1;
    return chdir(tmpdir);
}

static inline int th_leave_temp_dir(const char *saved_cwd, const char *tmpdir) {
    if (chdir(saved_cwd) != 0)
        return -1;
    return rmdir(tmpdir);
}

#endif /* TEST_HELPERS_H */
