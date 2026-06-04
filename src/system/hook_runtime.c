/*
 * hook_runtime.c — Lifecycle hook execution engine.
 *
 * Forks a subprocess for each matching hook, pipes JSON context
 * to stdin, and captures stdout. Exit code 2 = block decision.
 * Tracks timing, block counts, errors, and timeouts.
 */

#include "nevercode.h"
#include "defaults.h"

#include <fnmatch.h>

static struct {
    int total_runs;
    int total_blocks;
    int total_errors;
    int total_timeouts;
    long total_elapsed_ms;
} g_hstats;

static bool match_pat(const char *pat, const char *tool) {
    if (!pat || !pat[0])
        return true;
    if (!tool)
        return false;
    return fnmatch(pat, tool, FNM_NOESCAPE) == 0;
}

hook_decision_t hook_runtime_execute(hook_ctx_t *h, hook_event_t event, const char *tool_name,
                                     const char *tool_input, char **additional_ctx) {
    if (!h || h->num_hooks == 0) {
        if (additional_ctx)
            *additional_ctx = NULL;
        return HOOK_DECISION_PROCEED;
    }

    strbuf_t cb;
    strbuf_init(&cb);
    hook_decision_t final = HOOK_DECISION_PROCEED;

    for (int i = 0; i < h->num_hooks; i++) {
        hook_def_t *hd = &h->hooks[i];
        if (hd->event != event)
            continue;
        if (!match_pat(hd->tool_pattern, tool_name))
            continue;

        /* Build JSON env for the hook process */
        cJSON *env = cJSON_CreateObject();
        const char *en[] = {"pre_tool_use", "post_tool_use", "stop",
                            "pre_compact",  "teammate_idle", "task_completed"};
        int ei = (int)event;
        if (ei < 0 || ei > 5)
            ei = 0;
        cJSON_AddStringToObject(env, "event", en[ei]);
        if (tool_name)
            cJSON_AddStringToObject(env, "tool_name", tool_name);
        if (tool_input)
            cJSON_AddStringToObject(env, "tool_input_raw", tool_input);

        char *ej = nc_cjson_detach(env);

        /* Set up pipes */
        int pout[2], pin[2];
        if (nc_pipe_cloexec(pout) != 0) {
            free(ej);
            continue;
        }
        if (nc_pipe_cloexec(pin) != 0) {
            close(pout[0]);
            close(pout[1]);
            free(ej);
            continue;
        }

        long t0_ms = nc_mono_ms();

        pid_t pid = fork();
        if (pid < 0) {
            close(pout[0]);
            close(pout[1]);
            close(pin[0]);
            close(pin[1]);
            free(ej);
            continue;
        }

        if (pid == 0) {
            /* Child: wire stdin/stdout and exec */
            close(pin[1]);
            close(pout[0]);
            dup2(pin[0], 0);
            dup2(pout[1], 1);
            dup2(pout[1], 2);
            close(pin[0]);
            close(pout[1]);
            execl("/bin/sh", "sh", "-c", hd->command, (char *)NULL);
            _exit(127);
        }

        /* Parent: feed input and read output */
        close(pin[0]);
        close(pout[1]);

        if (ej) {
            ssize_t wn = write(pin[1], ej, strlen(ej));
            (void)wn;
        }
        close(pin[1]);
        free(ej);

        strbuf_t out;
        strbuf_init(&out);
        char buf[NC_IO_BUF];
        int timeout = hd->timeout > 0 ? hd->timeout : HOOK_DEFAULT_TIMEOUT;
        time_t dl = nc_time_now() + timeout;
        bool to = false;
        int ec = -1;

        while (1) {
            if (nc_time_now() >= dl) {
                to = true;
                kill(pid, SIGTERM);
                break;
            }
            fd_set rfds;
            struct timeval tv = {PROC_SELECT_POLL_SEC, 0};
            FD_ZERO(&rfds);
            if (pout[0] >= FD_SETSIZE)
                break;
            FD_SET(pout[0], &rfds);

            int sel = select(pout[0] + 1, &rfds, NULL, NULL, &tv);
            if (sel > 0) {
                ssize_t n = read(pout[0], buf, sizeof(buf) - 1);
                if (n <= 0)
                    break;
                buf[n] = '\0';
                strbuf_append(&out, buf, (size_t)n);
            } else if (sel == 0) {
                int st;
                if (waitpid(pid, &st, WNOHANG) == pid) {
                    ssize_t n;
                    while ((n = read(pout[0], buf, sizeof(buf) - 1)) > 0) {
                        buf[n] = '\0';
                        strbuf_append(&out, buf, (size_t)n);
                    }
                    ec = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
                    goto fin;
                }
            }
        }

        {
            int st;
            waitpid(pid, &st, 0);
            ec = to ? -2 : (WIFEXITED(st) ? WEXITSTATUS(st) : -1);
        }
        if (to)
            g_hstats.total_timeouts++;

    fin:
        close(pout[0]);

        g_hstats.total_elapsed_ms += nc_mono_ms() - t0_ms;
        g_hstats.total_runs++;

        if (ec == 2) {
            final = HOOK_DECISION_BLOCK;
            g_hstats.total_blocks++;
        } else if (ec != 0) {
            g_hstats.total_errors++;
        }

        if (out.len > 0) {
            if (cb.len > 0)
                strbuf_append_str(&cb, "\n");
            strbuf_append_str(&cb, out.data);
        }
        strbuf_free(&out);
    }

    if (additional_ctx) {
        if (cb.len > 0)
            *additional_ctx = strbuf_detach(&cb);
        else {
            *additional_ctx = NULL;
            strbuf_free(&cb);
        }
    } else {
        strbuf_free(&cb);
    }

    return final;
}

/* ── Stats ── */

void hook_runtime_reset_stats(void) {
    memset(&g_hstats, 0, sizeof(g_hstats));
}

int hook_runtime_total_runs(void) {
    return g_hstats.total_runs;
}

char *hook_runtime_format_stats(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Hook Runtime: %d runs, %d blocks, %d errors, %d timeouts, %ldms total",
                   g_hstats.total_runs, g_hstats.total_blocks, g_hstats.total_errors,
                   g_hstats.total_timeouts, g_hstats.total_elapsed_ms);
    return strbuf_detach(&sb);
}
