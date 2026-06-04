/*
 * clipboard.c — system clipboard integration (fork/exec, no shell)
 *
 * Reads text/image from system clipboard on macOS (pbpaste)
 * and Linux (xclip/xsel/wl-paste). Image data is saved
 * to a temp file and the path is returned.
 */

#include "nevercode.h"
#include "defaults.h"

static char *fork_capture(const char *const argv[]) {
    int pfd[2];
    if (nc_pipe_cloexec(pfd) < 0)
        return NULL;
    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return NULL;
    }
    if (pid == 0) {
        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);
        int nul = nc_open_devnull(O_WRONLY);
        if (nul >= 0) {
            dup2(nul, STDERR_FILENO);
            close(nul);
        }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    close(pfd[1]);
    strbuf_t sb;
    strbuf_init(&sb);
    char buf[NC_IO_BUF];
    ssize_t n;
    while ((n = read(pfd[0], buf, sizeof(buf))) > 0)
        strbuf_append(&sb, buf, (size_t)n);
    close(pfd[0]);
    int st;
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0 || sb.len == 0) {
        strbuf_free(&sb);
        return NULL;
    }
    return strbuf_detach(&sb);
}

#ifndef __APPLE__
static int fork_to_file(const char *const argv[], const char *out_path) {
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, NC_FILE_MODE);
        if (fd < 0)
            _exit(127);
        dup2(fd, STDOUT_FILENO);
        close(fd);
        int nul = nc_open_devnull(O_WRONLY);
        if (nul >= 0) {
            dup2(nul, STDERR_FILENO);
            close(nul);
        }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    int st;
    waitpid(pid, &st, 0);
    return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -1;
}
#endif

static int fork_quiet(const char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        int nul = nc_open_devnull(O_WRONLY);
        if (nul >= 0) {
            dup2(nul, STDOUT_FILENO);
            dup2(nul, STDERR_FILENO);
            close(nul);
        }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    int st;
    waitpid(pid, &st, 0);
    return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -1;
}

char *clipboard_read_text(void) {
#ifdef __APPLE__
    const char *argv[] = {"pbpaste", NULL};
    return fork_capture(argv);
#else
    if (nc_cmd_exists("xclip")) {
        const char *argv[] = {"xclip", "-selection", "clipboard", "-o", NULL};
        char *r = fork_capture(argv);
        if (r)
            return r;
    }
    if (nc_cmd_exists("xsel")) {
        const char *argv[] = {"xsel", "--clipboard", "--output", NULL};
        char *r = fork_capture(argv);
        if (r)
            return r;
    }
    if (nc_cmd_exists("wl-paste")) {
        const char *argv[] = {"wl-paste", NULL};
        return fork_capture(argv);
    }
    return NULL;
#endif
}

char *clipboard_read_image(void) {
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "/tmp/nc_clip_%d.png", (int)getpid());

#ifdef __APPLE__
    char set_png[] = "set png to (the clipboard as «class PNGf»)";
    char open_fp[NC_PATH_MAX + 80];
    snprintf(open_fp, sizeof(open_fp),
             "set fp to open for access POSIX file \"%s\" with write permission", path);
    const char *argv[] = {"osascript",       "-e", set_png,           "-e", open_fp, "-e",
                          "write png to fp", "-e", "close access fp", NULL};
    if (fork_quiet(argv) != 0)
        return NULL;
#else
    bool got = false;
    if (!got && nc_cmd_exists("xclip")) {
        const char *argv[] = {"xclip", "-selection", "clipboard", "-t", "image/png", "-o", NULL};
        got = (fork_to_file(argv, path) == 0);
    }
    if (!got && nc_cmd_exists("wl-paste")) {
        const char *argv[] = {"wl-paste", "--type", "image/png", NULL};
        got = (fork_to_file(argv, path) == 0);
    }
    if (!got)
        return NULL;
#endif

    struct stat st;
    if (stat(path, &st) != 0 || st.st_size < CB_MIN_IMAGE_BYTES)
        return NULL;

    return nc_xstrdup(path);
}

bool clipboard_write_text(const char *text, size_t len) {
    if (!text || len == 0)
        return false;
    int pfd[2];
    if (nc_pipe_cloexec(pfd) < 0)
        return false;
    pid_t pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return false;
    }
    if (pid == 0) {
        close(pfd[1]);
        dup2(pfd[0], STDIN_FILENO);
        close(pfd[0]);
        int nul = nc_open_devnull(O_WRONLY);
        if (nul >= 0) {
            dup2(nul, STDOUT_FILENO);
            dup2(nul, STDERR_FILENO);
            close(nul);
        }
#ifdef __APPLE__
        execlp("pbcopy", "pbcopy", (char *)NULL);
#else
        if (nc_cmd_exists("xclip"))
            execlp("xclip", "xclip", "-selection", "clipboard", (char *)NULL);
        else if (nc_cmd_exists("xsel"))
            execlp("xsel", "xsel", "--clipboard", "--input", (char *)NULL);
        else if (nc_cmd_exists("wl-copy"))
            execlp("wl-copy", "wl-copy", (char *)NULL);
#endif
        _exit(127);
    }
    close(pfd[0]);
    const char *p = text;
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t w = write(pfd[1], p, remaining);
        if (w <= 0)
            break;
        p += w;
        remaining -= (size_t)w;
    }
    close(pfd[1]);
    int st;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0 && remaining == 0;
}

bool clipboard_available(void) {
#ifdef __APPLE__
    return true;
#else
    return (nc_cmd_exists("xclip") || nc_cmd_exists("xsel") || nc_cmd_exists("wl-paste"));
#endif
}

bool clipboard_has_image(void) {
#ifdef __APPLE__
    const char *argv[] = {"osascript", "-e", "the clipboard as «class PNGf»", NULL};
    return fork_quiet(argv) == 0;
#else
    if (nc_cmd_exists("xclip")) {
        const char *argv[] = {"xclip", "-selection", "clipboard", "-t", "TARGETS", "-o", NULL};
        char *out = fork_capture(argv);
        if (out) {
            bool found = (strstr(out, "image/png") != NULL);
            free(out);
            if (found)
                return true;
        }
    }
    if (nc_cmd_exists("wl-paste")) {
        const char *argv[] = {"wl-paste", "--list-types", NULL};
        char *out = fork_capture(argv);
        if (out) {
            bool found = (strstr(out, "image/png") != NULL);
            free(out);
            return found;
        }
    }
    return false;
#endif
}
