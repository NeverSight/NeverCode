#include "nevercode.h"
#include "terminal.h"
#include <poll.h>
#include <fnmatch.h>

void hooks_init(hook_ctx_t *h) {
    memset(h, 0, sizeof(*h));
}

void hooks_free(hook_ctx_t *h) {
    for (int i = 0; i < h->num_hooks; i++) {
        free(h->hooks[i].command);
        free(h->hooks[i].tool_pattern);
    }
    h->num_hooks = 0;
}

static hook_event_t parse_event(const char *s) {
    if (!s)
        return HOOK_PRE_TOOL_USE;
    if (strcmp(s, "PreToolUse") == 0)
        return HOOK_PRE_TOOL_USE;
    if (strcmp(s, "PostToolUse") == 0)
        return HOOK_POST_TOOL_USE;
    if (strcmp(s, "Stop") == 0)
        return HOOK_STOP;
    if (strcmp(s, "PreCompact") == 0)
        return HOOK_PRE_COMPACT;
    return HOOK_PRE_TOOL_USE;
}

int hooks_load(hook_ctx_t *h, const char *path) {
    cJSON *root = nc_json_load(path);
    if (!root)
        return -1;

    cJSON *hooks_arr = cJSON_GetObjectItem(root, "hooks");
    if (!hooks_arr)
        hooks_arr = root;
    if (!cJSON_IsArray(hooks_arr)) {
        cJSON_Delete(root);
        return -1;
    }

    int nh = cJSON_GetArraySize(hooks_arr);
    for (int i = 0; i < nh && h->num_hooks < MAX_HOOKS; i++) {
        cJSON *hj = cJSON_GetArrayItem(hooks_arr, i);
        const char *event_str = cJSON_GetStringValue(cJSON_GetObjectItem(hj, "event"));
        const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(hj, "command"));
        if (!cmd)
            continue;

        hook_def_t *hd = &h->hooks[h->num_hooks++];
        hd->event = parse_event(event_str);
        hd->command = nc_xstrdup(cmd);

        const char *pattern = cJSON_GetStringValue(cJSON_GetObjectItem(hj, "toolPattern"));
        hd->tool_pattern = pattern ? nc_xstrdup(pattern) : NULL;

        cJSON *to = cJSON_GetObjectItem(hj, "timeout");
        hd->timeout = (to && cJSON_IsNumber(to)) ? (int)to->valuedouble : HOOK_DEFAULT_TIMEOUT;
    }

    cJSON_Delete(root);
    return h->num_hooks;
}

static bool hook_matches(const hook_def_t *hd, hook_event_t event, const char *tool_name) {
    if (hd->event != event)
        return false;
    if (!hd->tool_pattern)
        return true;
    if (!tool_name)
        return true;
    return fnmatch(hd->tool_pattern, tool_name, 0) == 0;
}

hook_decision_t hooks_run(hook_ctx_t *h, hook_event_t event, const char *tool_name,
                          const char *tool_input_json, char **additional_context) {
    if (additional_context)
        *additional_context = NULL;

    for (int i = 0; i < h->num_hooks; i++) {
        if (!hook_matches(&h->hooks[i], event, tool_name))
            continue;

        int pipefd[2];
        if (nc_pipe_cloexec(pipefd) < 0)
            continue;

        pid_t pid = fork();
        if (pid < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            continue;
        }

        if (pid == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);

            if (tool_name)
                setenv(ENV_HOOK_TOOL_NAME, tool_name, 1);
            if (tool_input_json)
                setenv(ENV_HOOK_TOOL_INPUT, tool_input_json, 1);

            const char *event_names[] = {"PreToolUse", "PostToolUse", "Stop", "PreCompact"};
            int ei = (int)event;
            if (ei < 0 || ei >= NC_ARRAY_LEN(event_names))
                ei = 0;
            setenv(ENV_HOOK_EVENT, event_names[ei], 1);

            execl("/bin/sh", "sh", "-c", h->hooks[i].command, (char *)NULL);
            _exit(127);
        }

        close(pipefd[1]);
        nc_set_nonblock(pipefd[0]);

        strbuf_t sb;
        strbuf_init(&sb);
        struct pollfd pfd = {.fd = pipefd[0], .events = POLLIN};
        int timeout = h->hooks[i].timeout;
        time_t start = nc_time_now();

        while (1) {
            int remaining = timeout - (int)(nc_time_now() - start);
            if (remaining <= 0)
                break;
            int ms = remaining > 1 ? 1000 : remaining * 1000;
            int r = poll(&pfd, 1, ms);
            if (r < 0 && errno == EINTR)
                continue;
            if (r <= 0)
                continue;
            char buf[NC_IO_BUF];
            ssize_t nr = read(pipefd[0], buf, sizeof(buf));
            if (nr < 0 && (errno == EAGAIN || errno == EINTR))
                continue;
            if (nr <= 0)
                break;
            strbuf_append(&sb, buf, nr);
        }
        close(pipefd[0]);

        int status = 0;
        waitpid(pid, &status, 0);
        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;

        /* Exit code semantics:
           0 = proceed (allow)
           2 = block (reject the tool call)
           3 = skip (proceed silently, no prompt)
           other nonzero = block with error message */

        if (sb.len > 0 && additional_context) {
            *additional_context = strbuf_detach(&sb);
        } else {
            strbuf_free(&sb);
        }

        if (exit_code == 2) {
            term_color(T_DIM, "[hook blocked: %s]\n", h->hooks[i].command);
            return HOOK_DECISION_BLOCK;
        }
        if (exit_code == 3)
            return HOOK_DECISION_SKIP;
        if (exit_code != 0) {
            term_color(T_YELLOW, "[hook error (exit %d): %s]\n", exit_code, h->hooks[i].command);
            return HOOK_DECISION_BLOCK;
        }
    }
    return HOOK_DECISION_PROCEED;
}
