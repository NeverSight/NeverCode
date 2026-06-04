/* proc_harden.c -- Process hardening for security. */
#include "nevercode.h"
#include "defaults.h"
#include <sys/resource.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#ifdef __APPLE__
#include <sys/ptrace.h>
#endif

static struct {
    bool core_dump_disabled;
    bool ptrace_denied;
    bool rlimit_set;
    int env_vars_cleared;
} g_ph;

static const char *denv[] = {
    ENV_LD_PRELOAD,        ENV_LD_LIBRARY_PATH,     ENV_LD_AUDIT, ENV_DYLD_INSERT_LIBRARIES,
    ENV_DYLD_LIBRARY_PATH, ENV_DYLD_FRAMEWORK_PATH, NULL};

int proc_harden_init(void) {
    memset(&g_ph, 0, sizeof(g_ph));
    int e = 0;

    struct rlimit rl = {0, 0};
    if (setrlimit(RLIMIT_CORE, &rl) == 0)
        g_ph.core_dump_disabled = true;
    else
        e++;

#ifdef __linux__
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) == 0)
        g_ph.ptrace_denied = true;
    else
        e++;
#elif defined(__APPLE__)
    if (ptrace(PT_DENY_ATTACH, 0, 0, 0) == 0 || errno == ENOTSUP)
        g_ph.ptrace_denied = true;
    else
        e++;
#endif

    for (int i = 0; denv[i]; i++) {
        if (getenv(denv[i])) {
            unsetenv(denv[i]);
            g_ph.env_vars_cleared++;
        }
    }

    struct rlimit nf;
    if (getrlimit(RLIMIT_NOFILE, &nf) == 0) {
        nf.rlim_cur = nf.rlim_max;
        setrlimit(RLIMIT_NOFILE, &nf);
    }
    g_ph.rlimit_set = true;
    return e;
}

bool proc_harden_core_dumps_off(void) {
    return g_ph.core_dump_disabled;
}
bool proc_harden_ptrace_denied(void) {
    return g_ph.ptrace_denied;
}
int proc_harden_env_cleared(void) {
    return g_ph.env_vars_cleared;
}

char *proc_harden_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb,
                   "Process Hardening:\n"
                   "  Core dumps: %s\n"
                   "  Ptrace: %s\n"
                   "  Env vars cleared: %d\n"
                   "  Resource limits: %s\n",
                   g_ph.core_dump_disabled ? "disabled" : "unchanged",
                   g_ph.ptrace_denied ? "denied" : "unchanged", g_ph.env_vars_cleared,
                   g_ph.rlimit_set ? "set" : "skipped");
    return strbuf_detach(&sb);
}
