/*
 * os_sandbox.c — OS-level sandboxing via native kernel mechanisms.
 *
 * macOS:  Seatbelt (sandbox_init) — kernel-enforced Scheme profile.
 * Linux:  Landlock LSM — unprivileged filesystem access control.
 *
 * Call os_sandbox_apply() in a child process (after fork, before exec)
 * to permanently restrict that process to the configured path grants.
 * The parent process is never restricted.
 */

#include "nevercode.h"

#ifdef __APPLE__
/*
 * Avoid #include <sandbox.h> — its sandbox_init() conflicts with ours.
 * Declare the macOS Seatbelt API under alternate C names via __asm__.
 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
extern int seatbelt_init(const char *profile, uint64_t flags,
                         char **errorbuf) __asm__("_sandbox_init");
extern void seatbelt_free_error(char *errorbuf) __asm__("_sandbox_free_error");
#pragma clang diagnostic pop
#endif

#ifdef __linux__
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <linux/landlock.h>

/* Flags added in later ABI versions may be missing from older headers */
#ifndef LANDLOCK_ACCESS_FS_REFER
#define LANDLOCK_ACCESS_FS_REFER (1ULL << 13)
#endif
#ifndef LANDLOCK_ACCESS_FS_TRUNCATE
#define LANDLOCK_ACCESS_FS_TRUNCATE (1ULL << 14)
#endif

#ifndef landlock_create_ruleset
static inline int landlock_create_ruleset(const struct landlock_ruleset_attr *attr, size_t size,
                                          __u32 flags) {
    return (int)syscall(__NR_landlock_create_ruleset, attr, size, flags);
}
#endif
#ifndef landlock_add_rule
static inline int landlock_add_rule(int ruleset_fd, enum landlock_rule_type type, const void *attr,
                                    __u32 flags) {
    return (int)syscall(__NR_landlock_add_rule, ruleset_fd, type, attr, flags);
}
#endif
#ifndef landlock_restrict_self
static inline int landlock_restrict_self(int ruleset_fd, __u32 flags) {
    return (int)syscall(__NR_landlock_restrict_self, ruleset_fd, flags);
}
#endif

#define LL_ACCESS_FS_RO                                                                            \
    (LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR)

#define LL_ACCESS_FS_RW                                                                            \
    (LL_ACCESS_FS_RO | LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_REMOVE_DIR |             \
     LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR | \
     LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SOCK | LANDLOCK_ACCESS_FS_MAKE_FIFO |   \
     LANDLOCK_ACCESS_FS_MAKE_BLOCK | LANDLOCK_ACCESS_FS_MAKE_SYM)
#endif

typedef enum {
    OS_SANDBOX_NONE = 0,
    OS_SANDBOX_SEATBELT,
    OS_SANDBOX_LANDLOCK,
} os_sandbox_type_t;

typedef struct {
    char *path;
    bool writable;
} sandbox_path_grant_t;

static struct {
    os_sandbox_type_t type;
    sandbox_profile_t profile;
    bool active;
    bool available;
    sandbox_path_grant_t paths[OS_SANDBOX_MAX_PATHS];
    int path_count;
    bool network_allowed;
    bool subprocess_allowed;
    int applied_count;
} g_os_sb;

static os_sandbox_type_t _detect_platform(void) {
#ifdef __APPLE__
    return OS_SANDBOX_SEATBELT;
#elif defined(__linux__)
    {
        int abi =
            (int)syscall(__NR_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
        if (abi > 0)
            return OS_SANDBOX_LANDLOCK;
    }
    return OS_SANDBOX_NONE;
#else
    return OS_SANDBOX_NONE;
#endif
}

void os_sandbox_init(void) {
    memset(&g_os_sb, 0, sizeof(g_os_sb));
    g_os_sb.type = _detect_platform();
    g_os_sb.available = (g_os_sb.type != OS_SANDBOX_NONE);
    g_os_sb.network_allowed = true;
    g_os_sb.subprocess_allowed = true;
    g_os_sb.profile = SANDBOX_PROFILE_FULL_ACCESS;
}

void os_sandbox_free(void) {
    for (int i = 0; i < g_os_sb.path_count; i++)
        free(g_os_sb.paths[i].path);
    memset(&g_os_sb, 0, sizeof(g_os_sb));
}

bool os_sandbox_available(void) {
    return g_os_sb.available;
}

const char *os_sandbox_type_name(void) {
    switch (g_os_sb.type) {
    case OS_SANDBOX_NONE:
        return "none";
    case OS_SANDBOX_SEATBELT:
        return "seatbelt";
    case OS_SANDBOX_LANDLOCK:
        return "landlock";
    }
    return "unknown";
}

int os_sandbox_add_path(const char *path, bool writable) {
    if (!path || g_os_sb.path_count >= OS_SANDBOX_MAX_PATHS)
        return -1;
    sandbox_path_grant_t *g = &g_os_sb.paths[g_os_sb.path_count++];
    g->path = nc_xstrdup(path);
    g->writable = writable;
    return 0;
}

void os_sandbox_set_network(bool allowed) {
    g_os_sb.network_allowed = allowed;
}

void os_sandbox_set_subprocess(bool allowed) {
    g_os_sb.subprocess_allowed = allowed;
}

bool os_sandbox_active(void) {
    return g_os_sb.active;
}

int os_sandbox_applied_count(void) {
    return g_os_sb.applied_count;
}

int os_sandbox_path_count(void) {
    return g_os_sb.path_count;
}

const char *os_sandbox_profile_name(sandbox_profile_t p) {
    switch (p) {
    case SANDBOX_PROFILE_FULL_ACCESS:
        return "full";
    case SANDBOX_PROFILE_READONLY:
        return "read-only";
    case SANDBOX_PROFILE_NO_NETWORK:
        return "network-deny";
    case SANDBOX_PROFILE_LOCKDOWN:
        return "minimal";
    }
    return "unknown";
}

/* ── macOS Seatbelt profile generation ── */

#ifdef __APPLE__

/* Append a subpath rule, also resolving symlinks (Seatbelt uses real paths) */
static void _sb_subpath(strbuf_t *sb, const char *op, const char *path) {
    strbuf_appendf(sb, "(allow %s (subpath \"%s\"))\n", op, path);
    char real[NC_PATH_MAX];
    if (realpath(path, real) && strcmp(real, path) != 0)
        strbuf_appendf(sb, "(allow %s (subpath \"%s\"))\n", op, real);
}

static char *_seatbelt_profile(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    /* ── Deny-all baseline ── */
    strbuf_append_str(&sb, "(version 1)\n(deny default)\n");

    /* ── Process lifecycle (scoped to same-sandbox) ── */
    strbuf_append_str(&sb, "(allow process-exec)\n");
    strbuf_append_str(&sb, "(allow process-fork)\n");
    strbuf_append_str(&sb, "(allow signal (target same-sandbox))\n");
    strbuf_append_str(&sb, "(allow process-info* (target same-sandbox))\n");

    /* ── System facilities ── */
    strbuf_append_str(&sb, "(allow sysctl-read)\n");
    strbuf_append_str(&sb, "(allow mach*)\n");
    strbuf_append_str(&sb, "(allow ipc*)\n");
    strbuf_append_str(&sb, "(allow user-preference-read)\n");
    strbuf_append_str(&sb, "(allow file-map-executable)\n");
    strbuf_append_str(&sb,
                      "(allow iokit-open (iokit-user-client-class \"RootDomainUserClient\"))\n");

    /* ── Minimal device writes (/dev/null, /dev/tty*, /dev/ptmx, /dev/urandom) ── */
    strbuf_append_str(&sb, "(allow file-write-data\n"
                           "  (literal \"/dev/null\")\n"
                           "  (literal \"/dev/dtracehelper\")\n"
                           "  (regex #\"/dev/ttys[0-9]+\")\n"
                           "  (literal \"/dev/ptmx\"))\n");
    strbuf_append_str(&sb, "(allow file-read*\n"
                           "  (literal \"/dev/null\")\n"
                           "  (literal \"/dev/urandom\")\n"
                           "  (literal \"/dev/random\")\n"
                           "  (regex #\"/dev/ttys[0-9]+\")\n"
                           "  (literal \"/dev/ptmx\")\n"
                           "  (literal \"/dev/tty\"))\n");

    /* ── System read-only paths for shell/tools/dyld ── */
    static const char *sys_ro[] = {"/usr",         "/bin",    "/sbin",
                                   "/Library",     "/System", "/private/var/db/dyld",
                                   "/private/etc", NULL};
    for (int i = 0; sys_ro[i]; i++)
        strbuf_appendf(&sb, "(allow file-read* (subpath \"%s\"))\n", sys_ro[i]);

    /* dyld and path resolution need metadata on root anchors */
    strbuf_append_str(&sb, "(allow file-read-metadata\n"
                           "  (literal \"/\")\n"
                           "  (literal \"/private\")\n"
                           "  (literal \"/var\")\n"
                           "  (literal \"/etc\")\n"
                           "  (literal \"/tmp\"))\n");

    /* ── Configured path grants (project root, /tmp, etc.) ── */
    for (int i = 0; i < g_os_sb.path_count; i++) {
        const sandbox_path_grant_t *g = &g_os_sb.paths[i];
        _sb_subpath(&sb, "file-read*", g->path);
        if (g->writable)
            _sb_subpath(&sb, "file-write*", g->path);
    }

    /*
     * Protected subdirectories: even inside writable roots, .git/ and
     * config directories must stay read-only to prevent privilege escalation
     * (e.g. modifying git hooks or our own sandbox config).
     */
    for (int i = 0; i < g_os_sb.path_count; i++) {
        const sandbox_path_grant_t *g = &g_os_sb.paths[i];
        if (!g->writable)
            continue;
        static const char *protected[] = {".git", CONFIG_DOTDIR_SECONDARY, NULL};
        for (int j = 0; protected[j]; j++) {
            char prot[NC_PATH_MAX];
            snprintf(prot, sizeof(prot), "%s/%s", g->path, protected[j]);
            strbuf_appendf(&sb, "(deny file-write* (subpath \"%s\"))\n", prot);
            char real[NC_PATH_MAX];
            if (realpath(prot, real) && strcmp(real, prot) != 0)
                strbuf_appendf(&sb, "(deny file-write* (subpath \"%s\"))\n", real);
        }
    }

    const char *tmpdir = nc_tmpdir();
    if (tmpdir) {
        _sb_subpath(&sb, "file-read*", tmpdir);
        _sb_subpath(&sb, "file-write*", tmpdir);
    }

    /* ── Home config dirs: read-only access ── */
    const char *home = nc_home_dir();
    if (home) {
        static const char *config_dirs[] = {CONFIG_DOTDIR_SECONDARY, CONFIG_DOTDIR_COMPAT,
                                            CONFIG_DOTDIR_PRIMARY, ".config", NULL};
        for (int i = 0; config_dirs[i]; i++) {
            char cdir[NC_PATH_MAX];
            snprintf(cdir, sizeof(cdir), "%s/%s", home, config_dirs[i]);
            _sb_subpath(&sb, "file-read*", cdir);
        }
        /* Shell rc files for login shells */
        static const char *rc_files[] = {".zshrc",  ".bashrc",       ".profile", ".zprofile",
                                         ".zshenv", ".bash_profile", NULL};
        for (int i = 0; rc_files[i]; i++)
            strbuf_appendf(&sb, "(allow file-read* (literal \"%s/%s\"))\n", home, rc_files[i]);

        /* Metadata on home for path resolution */
        strbuf_appendf(&sb, "(allow file-read-metadata (literal \"%s\"))\n", home);
    }

    /* ── Network ── */
    if (g_os_sb.network_allowed) {
        strbuf_append_str(&sb, "(allow network*)\n");
    }

    return strbuf_detach(&sb);
}

static int _apply_seatbelt(void) {
    char *profile = _seatbelt_profile();
    if (!profile)
        return -1;

    char *errorbuf = NULL;
    int rc = seatbelt_init(profile, 0, &errorbuf);

    if (rc != 0) {
        if (errorbuf) {
            fprintf(stderr, "[sandbox] seatbelt error: %s\n", errorbuf);
            seatbelt_free_error(errorbuf);
        }
        free(profile);
        return -1;
    }
    free(profile);
    return 0;
}
#endif /* __APPLE__ */

/* ── Linux Landlock enforcement ── */

#ifdef __linux__
static int _ll_add_path_rule(int ruleset_fd, const char *path, __u64 allowed_access) {
    int fd = open(path, O_PATH | O_CLOEXEC);
    if (fd < 0)
        return -1;

    struct landlock_path_beneath_attr rule = {
        .allowed_access = allowed_access,
        .parent_fd = fd,
    };

    struct stat st;
    if (fstat(fd, &st) == 0 && !S_ISDIR(st.st_mode)) {
        /* File, not dir — restrict to file-level access only */
        rule.allowed_access &= (LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE |
                                LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_TRUNCATE);
    }

    int rc = landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH, &rule, 0);
    close(fd);
    return rc;
}

static int _apply_landlock(void) {
    int abi = (int)syscall(__NR_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < 0)
        return -1;

    __u64 handled = LL_ACCESS_FS_RW;
    if (abi < 2)
        handled &= ~LANDLOCK_ACCESS_FS_REFER;
    if (abi < 3)
        handled &= ~LANDLOCK_ACCESS_FS_TRUNCATE;

    struct landlock_ruleset_attr attr = {.handled_access_fs = handled};
    int ruleset_fd = landlock_create_ruleset(&attr, sizeof(attr), 0);
    if (ruleset_fd < 0)
        return -1;

    __u64 ro_access = LL_ACCESS_FS_RO & handled;
    __u64 rw_access = handled;

    /* System read-only paths */
    static const char *sys_ro[] = {"/usr", "/bin",  "/sbin", "/lib", "/lib64",
                                   "/etc", "/proc", "/sys",  NULL};
    for (int i = 0; sys_ro[i]; i++)
        _ll_add_path_rule(ruleset_fd, sys_ro[i], ro_access);

    /* Configured path grants */
    for (int i = 0; i < g_os_sb.path_count; i++) {
        const sandbox_path_grant_t *g = &g_os_sb.paths[i];
        _ll_add_path_rule(ruleset_fd, g->path, g->writable ? rw_access : ro_access);
    }

    /*
     * Protected subdirectories inside writable roots: .git/ and config
     * dirs get read-only even when the parent is writable.  Landlock
     * stacks layers, so a narrower read-only rule overrides the wider
     * read-write grant.
     */
    for (int i = 0; i < g_os_sb.path_count; i++) {
        const sandbox_path_grant_t *g = &g_os_sb.paths[i];
        if (!g->writable)
            continue;
        static const char *protected_dirs[] = {".git", CONFIG_DOTDIR_SECONDARY, NULL};
        for (int j = 0; protected_dirs[j]; j++) {
            char prot[NC_PATH_MAX];
            snprintf(prot, sizeof(prot), "%s/%s", g->path, protected_dirs[j]);
            _ll_add_path_rule(ruleset_fd, prot, ro_access);
        }
    }

    const char *tmpdir = nc_tmpdir();
    if (tmpdir)
        _ll_add_path_rule(ruleset_fd, tmpdir, rw_access);

    /* Home config dirs: read-only */
    const char *home = nc_home_dir();
    if (home) {
        char buf[NC_PATH_MAX];
        static const char *config_dirs[] = {CONFIG_DOTDIR_SECONDARY, CONFIG_DOTDIR_COMPAT,
                                            CONFIG_DOTDIR_PRIMARY, ".config", NULL};
        for (int j = 0; config_dirs[j]; j++) {
            snprintf(buf, sizeof(buf), "%s/%s", home, config_dirs[j]);
            _ll_add_path_rule(ruleset_fd, buf, ro_access);
        }
    }

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
        close(ruleset_fd);
        return -1;
    }

    int rc = landlock_restrict_self(ruleset_fd, 0);
    close(ruleset_fd);
    return rc;
}
#endif /* __linux__ */

/*
 * os_sandbox_apply — Apply kernel sandbox to the CURRENT process.
 *
 * Call in a child process after fork(), before exec().
 * Restrictions are permanent and cannot be removed.
 * Returns 0 on success, -1 on failure.
 */
int os_sandbox_apply(void) {
    if (!g_os_sb.available)
        return -1;

    int rc = -1;

#ifdef __APPLE__
    if (g_os_sb.type == OS_SANDBOX_SEATBELT)
        rc = _apply_seatbelt();
#endif

#ifdef __linux__
    if (g_os_sb.type == OS_SANDBOX_LANDLOCK)
        rc = _apply_landlock();
#endif

    if (rc == 0) {
        g_os_sb.active = true;
        g_os_sb.applied_count++;
    }
    return rc;
}

/*
 * os_sandbox_setup_paths — Configure standard path grants for project sandbox.
 *
 * Call before os_sandbox_apply(). Adds the project root (rw),
 * /tmp (rw), and /dev (ro) as allowed paths.
 */
void os_sandbox_setup_paths(const char *project_root) {
    if (project_root)
        os_sandbox_add_path(project_root, true);
    os_sandbox_add_path("/tmp", true);
    os_sandbox_add_path("/dev", false);
    const char *tmpdir = nc_tmpdir();
    if (tmpdir && strcmp(tmpdir, "/tmp") != 0)
        os_sandbox_add_path(tmpdir, true);
}

char *os_sandbox_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "OS Sandbox: type=%s available=%s active=%s profile=%s\n",
                   os_sandbox_type_name(), g_os_sb.available ? "yes" : "no",
                   g_os_sb.active ? "yes" : "no", os_sandbox_profile_name(g_os_sb.profile));
    strbuf_appendf(&sb, "  paths=%d net=%s subprocess=%s applied=%d\n", g_os_sb.path_count,
                   g_os_sb.network_allowed ? "allow" : "deny",
                   g_os_sb.subprocess_allowed ? "allow" : "deny", g_os_sb.applied_count);
    return strbuf_detach(&sb);
}
