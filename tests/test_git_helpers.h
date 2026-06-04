#ifndef TEST_GIT_HELPERS_H
#define TEST_GIT_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

static inline void test_git_env_setup(void) {
    setenv("GIT_AUTHOR_NAME", "nevercode-test", 1);
    setenv("GIT_AUTHOR_EMAIL", "nevercode@example.com", 1);
    setenv("GIT_COMMITTER_NAME", "nevercode-test", 1);
    setenv("GIT_COMMITTER_EMAIL", "nevercode@example.com", 1);
    setenv("GIT_CONFIG_NOSYSTEM", "1", 1);
    setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1);
    setenv("GIT_TERMINAL_PROMPT", "0", 1);
}

static inline int test_git_run(const char *cwd, const char *const *argv) {
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        if (cwd && chdir(cwd) != 0)
            _exit(127);
        int fd = open("/dev/null", 0);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    int status;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static inline int test_git_write_file(const char *path, const char *text) {
    FILE *fp = fopen(path, "w");
    if (!fp)
        return -1;
    fputs(text, fp);
    fclose(fp);
    return 0;
}

static inline int test_git_init_repo(const char *dir) {
    test_git_env_setup();
    const char *init_argv[] = {"git", "init", "-q", NULL};
    const char *add_argv[] = {"git", "add", "note.txt", NULL};
    const char *commit_argv[] = {"git", "commit", "-q", "-m", "base", NULL};
    char note_path[4096];
    snprintf(note_path, sizeof(note_path), "%s/note.txt", dir);
    if (test_git_run(dir, init_argv) != 0)
        return -1;
    if (test_git_write_file(note_path, "initial\n") != 0)
        return -1;
    if (test_git_run(dir, add_argv) != 0)
        return -1;
    if (test_git_run(dir, commit_argv) != 0)
        return -1;
    return 0;
}

static inline void test_git_cleanup(const char *dir) {
    const char *argv[] = {"rm", "-rf", dir, NULL};
    test_git_run(NULL, argv);
}

#endif /* TEST_GIT_HELPERS_H */
