#include "nevercode.h"
#include "defaults.h"
#include <sys/resource.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

static hardened_limits_t g_dl = {.cpu_time_limit_s = HARDENED_CPU_LIMIT_S,
                                 .mem_limit_bytes = HARDENED_MEM_LIMIT,
                                 .output_limit_bytes = HARDENED_OUTPUT_LIMIT,
                                 .nice_value = HARDENED_NICE_VALUE,
                                 .new_session = true};

static int g_runs = 0, g_ooms = 0, g_timeouts = 0;

static long tv_ms(const struct timeval *v) {
    return (long)v->tv_sec * 1000 + v->tv_usec / 1000;
}

int hardened_exec(const char *cmd, const hardened_limits_t *lm, char **out,
                  hardened_result_t *res) {
    if (!cmd || !res)
        return -1;
    const hardened_limits_t *l = lm ? lm : &g_dl;
    memset(res, 0, sizeof(*res));

    int pf[2];
    if (nc_pipe_cloexec(pf))
        return -1;

    long wall_start = nc_mono_ms();

    pid_t p = fork();
    if (p < 0) {
        close(pf[0]);
        close(pf[1]);
        return -1;
    }

    if (p == 0) {
        close(pf[0]);
        dup2(pf[1], 1);
        dup2(pf[1], 2);
        close(pf[1]);
        if (l->new_session)
            setsid();
#ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
        if (l->nice_value) {
            if (nice(l->nice_value) == -1) {
                /* best effort: continue with default priority */
            }
        }
        if (l->cpu_time_limit_s > 0) {
            struct rlimit r = {.rlim_cur = (rlim_t)l->cpu_time_limit_s,
                               .rlim_max = (rlim_t)(l->cpu_time_limit_s + 5)};
            setrlimit(RLIMIT_CPU, &r);
        }
#ifdef RLIMIT_AS
        if (l->mem_limit_bytes > 0) {
            struct rlimit r = {.rlim_cur = l->mem_limit_bytes, .rlim_max = l->mem_limit_bytes};
            setrlimit(RLIMIT_AS, &r);
        }
#endif
        if (sandbox_enforcing() && os_sandbox_available())
            os_sandbox_apply();
        execlp("sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    close(pf[1]);
    strbuf_t sb;
    strbuf_init(&sb);
    char bf[NC_IO_BUF];
    size_t tot = 0;
    bool tr = false;

    while (1) {
        ssize_t n = read(pf[0], bf, sizeof(bf));
        if (n <= 0)
            break;
        tot += (size_t)n;
        if (!tr) {
            if (sb.len + (size_t)n > l->output_limit_bytes) {
                size_t r = l->output_limit_bytes - sb.len;
                if (r > 0)
                    strbuf_append(&sb, bf, r);
                tr = true;
            } else {
                strbuf_append(&sb, bf, (size_t)n);
            }
        }
    }
    close(pf[0]);

    int st = 0;
    struct rusage ru;
    wait4(p, &st, 0, &ru);

    res->exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    res->signal_num = WIFSIGNALED(st) ? WTERMSIG(st) : 0;
    res->timed_out = (res->signal_num == SIGXCPU || res->signal_num == SIGKILL);
    res->oom_killed = (res->signal_num == SIGKILL && res->exit_code < 0);
    res->wall_ms = nc_mono_ms() - wall_start;
    res->user_ms = tv_ms(&ru.ru_utime);
    res->sys_ms = tv_ms(&ru.ru_stime);
    res->output_bytes = tot;
    res->output_truncated = tr;

    g_runs++;
    if (res->oom_killed)
        g_ooms++;
    if (res->timed_out)
        g_timeouts++;

    if (out) {
        if (tr)
            strbuf_appendf(&sb, "\n\n[output truncated: %zu bytes total, limit %zu]", tot,
                           l->output_limit_bytes);
        *out = strbuf_detach(&sb);
    } else {
        strbuf_free(&sb);
    }
    return 0;
}

int hardened_exec_default(const char *c, char **o, hardened_result_t *r) {
    return hardened_exec(c, NULL, o, r);
}

int hardened_exec_quick(const char *cmd, int timeout_s, char **out, hardened_result_t *res) {
    hardened_limits_t l = g_dl;
    if (timeout_s > 0)
        l.cpu_time_limit_s = timeout_s;
    return hardened_exec(cmd, &l, out, res);
}

void hardened_limits_default(hardened_limits_t *l) {
    *l = g_dl;
}

char *hardened_format_stats(void) {
    strbuf_t s;
    strbuf_init(&s);
    strbuf_appendf(&s, "hardened exec: %d runs, %d oom, %d timeouts", g_runs, g_ooms, g_timeouts);
    return strbuf_detach(&s);
}
