#include "tools.h"
#include "nevercode.h"
#include <poll.h>

/* Local alias for the centralized macro */
#define MAX_OUTPUT BASH_TOOL_OUTPUT_MAX

/* ── Command Semantics ──
 * Some commands use non-zero exit codes for informational results rather than
 * errors. Without this, the agent sees "[exit: 1]" from grep and believes
 * the command failed when it simply found no matches.
 */

typedef struct {
    const char *cmd;
    int info_max;          /* exit codes <= info_max are informational, not errors */
    const char *labels[4]; /* human label per exit code 1..info_max (NULL-terminated) */
} cmd_semantic_t;

static const cmd_semantic_t CMD_SEMANTICS[] = {{"grep", 1, {"no matches found", NULL}},
                                               {"egrep", 1, {"no matches found", NULL}},
                                               {"fgrep", 1, {"no matches found", NULL}},
                                               {"rg", 1, {"no matches found", NULL}},
                                               {"ag", 1, {"no matches found", NULL}},
                                               {"diff", 1, {"files differ", NULL}},
                                               {"cmp", 1, {"files differ", NULL}},
                                               {"test", 1, {"condition is false", NULL}},
                                               {"[", 1, {"condition is false", NULL}},
                                               {"find", 1, {"some directories inaccessible", NULL}},
                                               {NULL, 0, {NULL}}};

/* In a pipeline (a | b | c), the shell exit code comes from the last
 * command.  Walk the string to find it, skipping || (logical OR). */
static const char *extract_pipeline_tail(const char *cmd) {
    if (!cmd)
        return NULL;
    const char *last = cmd;
    for (const char *p = cmd; *p; p++) {
        if (*p == '|' && *(p + 1) != '|') {
            const char *q = p + 1;
            while (*q == ' ')
                q++;
            if (*q)
                last = q;
        }
    }
    return last;
}

static const char *cmd_semantic_label(const char *cmd, int exit_code) {
    if (!cmd || exit_code <= 0)
        return NULL;
    const char *s = extract_pipeline_tail(cmd);
    if (!s)
        return NULL;
    while (*s == ' ')
        s++;
    char first[NC_LABEL_MAX];
    int i = 0;
    while (s[i] && !isspace((unsigned char)s[i]) && i < (int)sizeof(first) - 1)
        first[i] = s[i], i++;
    first[i] = '\0';

    for (const cmd_semantic_t *cs = CMD_SEMANTICS; cs->cmd; cs++) {
        if (strcmp(first, cs->cmd) == 0 && exit_code <= cs->info_max)
            return cs->labels[exit_code - 1];
    }
    return NULL;
}

static const char *READONLY_CMDS[] = {
    "ls",       "cat",    "head",    "tail",      "wc",       "stat",     "file",     "which",
    "type",     "pwd",    "whoami",  "uname",     "id",       "hostname", "date",     "cal",
    "uptime",   "free",   "df",      "du",        "basename", "dirname",  "realpath", "readlink",
    "echo",     "printf", "true",    "false",     "test",     "expr",     "seq",      "diff",
    "cmp",      "comm",   "sort",    "uniq",      "cut",      "paste",    "tr",       "column",
    "tac",      "rev",    "fold",    "expand",    "unexpand", "fmt",      "nl",       "strings",
    "hexdump",  "od",     "sleep",   "arch",      "nproc",    "locale",   "groups",   "getconf",
    "tsort",    "pr",     "numfmt",  "sha256sum", "sha1sum",  "md5sum",   "base64",   "grep",
    "egrep",    "fgrep",  "rg",      "ag",        "locate",   "mlocate",  "yq",       "xq",
    "tree",     "exa",    "eza",     "lsd",       "bat",      "batcat",   "env",      "printenv",
    "top",      "htop",   "pidof",   "ifconfig",  "ip",       "dig",      "nslookup", "host",
    "apropos",  "whatis", "objdump", "readelf",   "nm",       "ldd",      "otool",    "xxd",
    "checksec", NULL};

static const char *READONLY_GIT_SUBCMDS[] = {
    "status",        "diff",          "log",           "show",
    "blame",         "branch",        "tag",           "ls-files",
    "ls-tree",       "ls-remote",     "remote",        "stash list",
    "config --get",  "config --list", "rev-parse",     "describe",
    "shortlog",      "reflog",        "name-rev",      "rev-list",
    "cat-file",      "for-each-ref",  "count-objects", "fsck",
    "verify-commit", "verify-tag",    "merge-base",    NULL};

bool bash_is_readonly(const char *cmd) {
    if (!cmd || !*cmd)
        return false;
    cmd = nc_skip_ws(cmd);

    if (strchr(cmd, '|') || strchr(cmd, '>') || strchr(cmd, '<') || strchr(cmd, '`') ||
        strstr(cmd, "$(") || strchr(cmd, ';') || strstr(cmd, "&&") || strstr(cmd, "||"))
        return false;

    if (strstr(cmd, "rm ") || strstr(cmd, "mv ") || strstr(cmd, "cp ") || strstr(cmd, "mkdir ") ||
        strstr(cmd, "chmod ") || strstr(cmd, "chown ") || strstr(cmd, "touch ") ||
        strstr(cmd, "tee ") || strstr(cmd, "wget ") || strstr(cmd, "curl ") ||
        strstr(cmd, "kill ") || strstr(cmd, "pkill ") || strstr(cmd, "sudo ") ||
        strstr(cmd, "su ") || strstr(cmd, "dd ") || strstr(cmd, "npm ") || strstr(cmd, "pip ") ||
        strstr(cmd, "brew ") || strstr(cmd, "apt ") || strstr(cmd, "yum ") || strstr(cmd, "dnf "))
        return false;

    char first_word[NC_LABEL_MAX] = {0};
    int i = 0;
    while (cmd[i] && !isspace((unsigned char)cmd[i]) && i < 255)
        first_word[i] = cmd[i], i++;
    first_word[i] = '\0';

    for (const char **p = READONLY_CMDS; *p; p++) {
        if (strcmp(first_word, *p) == 0)
            return true;
    }

    if (strcmp(first_word, "find") == 0) {
        if (strstr(cmd, "-exec") || strstr(cmd, "-delete") || strstr(cmd, "-ok") ||
            strstr(cmd, "-fprint"))
            return false;
        return true;
    }

    if (strcmp(first_word, "grep") == 0 || strcmp(first_word, "egrep") == 0 ||
        strcmp(first_word, "fgrep") == 0 || strcmp(first_word, "rg") == 0)
        return true;

    if (strcmp(first_word, "jq") == 0) {
        if (strstr(cmd, "--run-tests") || strstr(cmd, "-f ") || strstr(cmd, "--from-file"))
            return false;
        return true;
    }

    if (strcmp(first_word, "fd") == 0 || strcmp(first_word, "fdfind") == 0) {
        if (strstr(cmd, "-x ") || strstr(cmd, "--exec") || strstr(cmd, "-X ") ||
            strstr(cmd, "--exec-batch"))
            return false;
        return true;
    }

    if (strcmp(first_word, "ps") == 0 || strcmp(first_word, "pgrep") == 0 ||
        strcmp(first_word, "lsof") == 0 || strcmp(first_word, "netstat") == 0 ||
        strcmp(first_word, "ss") == 0 || strcmp(first_word, "tree") == 0 ||
        strcmp(first_word, "man") == 0 || strcmp(first_word, "info") == 0 ||
        strcmp(first_word, "help") == 0)
        return true;

    if (strcmp(first_word, "git") == 0) {
        const char *sub = nc_skip_ws(cmd + 3);
        if (strstr(cmd, " -c ") || strstr(cmd, " -c=") || strstr(cmd, "--exec-path"))
            return false;
        for (const char **p = READONLY_GIT_SUBCMDS; *p; p++) {
            if (nc_starts_with(sub, *p)) {
                char after = sub[strlen(*p)];
                if (after == '\0' || isspace((unsigned char)after))
                    return true;
            }
        }
    }

    if (strcmp(first_word, "docker") == 0) {
        const char *sub = nc_skip_ws(cmd + 6);
        if (nc_starts_with(sub, "ps") || nc_starts_with(sub, "images") ||
            nc_starts_with(sub, "inspect") || nc_starts_with(sub, "logs") ||
            nc_starts_with(sub, "info") || nc_starts_with(sub, "version") ||
            nc_starts_with(sub, "stats") || nc_starts_with(sub, "top") ||
            nc_starts_with(sub, "port") || nc_starts_with(sub, "diff") ||
            nc_starts_with(sub, "history"))
            return true;
    }

    return false;
}

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"command\":{\"type\":\"string\",\"description\":\"The bash command to run\"},"
    "\"timeout\":{\"type\":\"integer\",\"description\":\"Timeout in seconds (default " NC_STRINGIFY(
        DEFAULT_BASH_TIMEOUT) ")\"},"
                              "\"working_directory\":{\"type\":\"string\",\"description\":"
                              "\"Directory to run the command in\"},"
                              "\"run_in_background\":{\"type\":\"boolean\",\"description\":"
                              "\"Run command in background and return immediately with a PID. "
                              "Output streams to a log file. Use for long-running commands like "
                              "dev servers, watchers, or builds you do not need to block on.\"}"
                              "},"
                              "\"required\":[\"command\"]"
                              "}";

static int bash_exec_fork(const char *cmd, int timeout, const char *cwd, char **output) {
    int pipefd[2];
    if (nc_pipe_cloexec(pipefd) < 0) {
        *output = nc_xstrdup("error: pipe creation failed");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        *output = nc_xstrdup("error: fork() failed");
        return 1;
    }

    if (pid == 0) {
        setpgid(0, 0);
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        if (cwd && cwd[0] && chdir(cwd) != 0)
            _exit(126);
        if (sandbox_enforcing() && os_sandbox_available())
            os_sandbox_apply();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    setpgid(pid, pid);
    procgroup_register(procgroup_global(), pid, cmd, PROCGROUP_TOOL);

    close(pipefd[1]);
    nc_set_nonblock(pipefd[0]);

    strbuf_t sb;
    strbuf_init(&sb);
    struct pollfd pfd = {.fd = pipefd[0], .events = POLLIN};
    time_t start = nc_time_now();
    bool timed_out = false;

    while (1) {
        int remaining = timeout - (int)(nc_time_now() - start);
        if (remaining <= 0) {
            timed_out = true;
            break;
        }
        int ms = remaining > 1 ? 1000 : remaining * 1000;
        int r = poll(&pfd, 1, ms);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            continue;

        char buf[NC_IO_BUF];
        ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (n <= 0)
            break;
        if (sb.len + (size_t)n > MAX_OUTPUT) {
            strbuf_append(&sb, buf, MAX_OUTPUT - sb.len);
            strbuf_append_str(&sb, "\n... [output truncated]");
            break;
        }
        strbuf_append(&sb, buf, n);
    }

    close(pipefd[0]);

    int status = 0;
    if (timed_out) {
        killpg(pid, SIGTERM);
        usleep(PROC_TERM_GRACE_US);
        if (waitpid(pid, &status, WNOHANG) == 0) {
            killpg(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
        strbuf_append_str(&sb, "\n[timed out]");
    } else {
        waitpid(pid, &status, 0);
    }
    int exit_code;
    if (timed_out)
        exit_code = NC_EXIT_TIMEOUT;
    else if (WIFEXITED(status))
        exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        exit_code = NC_EXIT_SIGNAL(WTERMSIG(status));
    else
        exit_code = 1;

    procgroup_unregister(procgroup_global(), pid);

    if (sb.len == 0)
        strbuf_append_str(&sb, "(no output)");

    *output = strbuf_detach(&sb);
    return exit_code;
}

static bool cmd_needs_isolation(const char *cmd) {
    if (!cmd || !*cmd)
        return true;
    const char *s = cmd;
    while (*s == ' ')
        s++;

    if (nc_starts_with(s, "exit") && (s[4] == '\0' || s[4] == ' '))
        return true;
    if (nc_starts_with(s, "exec "))
        return true;
    if (nc_starts_with(s, "sleep ")) {
        int sec = nc_safe_atoi(s + 6, 0);
        if (sec > 5)
            return true;
    }
    if (strstr(cmd, "sudo ") || strstr(cmd, "nohup ") || strstr(cmd, " & ") ||
        (cmd[strlen(cmd) - 1] == '&'))
        return true;
    return false;
}

static int bash_exec_background(const char *cmd, const char *cwd, char **output) {
    char logpath[NC_PATH_MAX];
    const char *home = nc_home_dir();
    if (!home)
        home = "/tmp";
    snprintf(logpath, sizeof(logpath), "%s/.agents/bg_%ld.log", home, nc_wall_sec());

    char parent_dir[NC_PATH_MAX];
    snprintf(parent_dir, sizeof(parent_dir), "%s/.agents", home);
    mkdir(parent_dir, NC_DIR_MODE);

    pid_t pid = fork();
    if (pid < 0) {
        *output = nc_xstrdup("error: fork() failed for background exec");
        return 1;
    }

    if (pid == 0) {
        nc_child_setup();
        int fd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, NC_FILE_MODE);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        int devnull = nc_open_devnull(O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        if (cwd && cwd[0])
            (void)chdir(cwd);
        if (sandbox_enforcing() && os_sandbox_available())
            os_sandbox_apply();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    procgroup_register(procgroup_global(), pid, cmd, PROCGROUP_TOOL);

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb,
                   "Background process started (pid=%d)\nOutput log: %s\n"
                   "Use `cat %s` or `tail -f %s` to monitor.",
                   (int)pid, logpath, logpath, logpath);
    *output = strbuf_detach(&sb);
    return 0;
}

static int bash_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *cmd = nc_cjson_str((cJSON *)input, "command", NULL);
    if (!cmd) {
        *output = nc_xstrdup("error: missing 'command'");
        return 1;
    }

    cJSON *bg = cJSON_GetObjectItem((cJSON *)input, "run_in_background");
    if (bg && cJSON_IsTrue(bg)) {
        const char *cwd = nc_cjson_str((cJSON *)input, "working_directory", NULL);
        return bash_exec_background(cmd, cwd, output);
    }

    int timeout = DEFAULT_BASH_TIMEOUT;
    cJSON *to = cJSON_GetObjectItem((cJSON *)input, "timeout");
    if (to && cJSON_IsNumber(to))
        timeout = (int)to->valuedouble;
    if (timeout <= 0)
        timeout = DEFAULT_BASH_TIMEOUT;

    const char *cwd = nc_cjson_str((cJSON *)input, "working_directory", NULL);

    int exit_code;
    if (cmd_needs_isolation(cmd)) {
        exit_code = bash_exec_fork(cmd, timeout, cwd, output);
    } else {
        int sess_rc = shell_session_exec(cmd, cwd, timeout, output, &exit_code);
        if (sess_rc < 0)
            exit_code = bash_exec_fork(cmd, timeout, cwd, output);
    }

    /* Parse diagnostics from build tool output and append fix hints */
    if (exit_code != 0 && *output && strlen(*output) > 10) {
        diagnostic_entry_t diags[8];
        int nd = diagnostic_parse_output(*output, diags, 8);
        if (nd > 0) {
            int ann = diagnostic_annotate_suggestions(diags, nd);
            if (ann > 0) {
                strbuf_t sb;
                strbuf_init(&sb);
                strbuf_append_str(&sb, *output);
                strbuf_append_str(&sb, "\n\n[diagnostic hints]\n");
                char *fmt = diagnostic_format(diags, nd);
                strbuf_append_str(&sb, fmt);
                free(fmt);
                free(*output);
                *output = strbuf_detach(&sb);
            }
            diagnostics_free(diags, nd);
        }
    }

    if (*output) {
        size_t olen = strlen(*output);
        if (olen == 0) {
            free(*output);
            *output = nc_xstrdup("(no output)");
        }
    }

    /* Command semantics: reclassify informational exit codes so the agent
     * does not treat e.g. grep-returning-1 as a failure. */
    const char *sem_label = cmd_semantic_label(cmd, exit_code);
    bool is_informational = (sem_label != NULL);

    char footer[NC_TAG_MAX];
    if (is_informational)
        snprintf(footer, sizeof(footer), "\n[exit: %d — %s]", exit_code, sem_label);
    else
        snprintf(footer, sizeof(footer), "\n[exit: %d]", exit_code);

    strbuf_t final;
    strbuf_init(&final);
    strbuf_append_str(&final, *output ? *output : "(no output)");
    strbuf_append_str(&final, footer);
    free(*output);
    *output = strbuf_detach(&final);

    return is_informational ? 0 : exit_code;
}

static tool_def_t bash_tool;

const char *bash_semantic_label(const char *cmd, int exit_code) {
    return cmd_semantic_label(cmd, exit_code);
}

bool bash_check_readonly(const cJSON *input) {
    const char *cmd = nc_cjson_str((cJSON *)input, "command", NULL);
    return cmd && bash_is_readonly(cmd);
}

void tool_bash_register(void) {
    bash_tool.name = "Bash";
    bash_tool.description = "Run a bash command. Use for shell operations, running scripts, "
                            "installing packages, etc.";
    bash_tool.input_schema = cJSON_Parse(SCHEMA);
    bash_tool.execute = bash_exec;
    bash_tool.check_permission = NULL;
    bash_tool.read_only = false;
    bash_tool.concurrency_safe = false;
    bash_tool.destructive = false;
    tools_register(&bash_tool);
}
