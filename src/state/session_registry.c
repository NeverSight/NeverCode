/*
 * session_registry.c — Concurrent session PID file registry.
 *
 * Registers the current process in a sessions directory with a
 * PID file. Lists all live sessions for /ps. Cleans up stale
 * PID files from processes that exited without cleanup.
 */

#include "nevercode.h"

static struct {
    char pid_path[NC_PATH_MAX];
    bool registered;
} g_sr;

static void sessions_dir(char *out, size_t sz) {
    config_home_path(out, sz, "sessions");
}

static bool pid_alive(pid_t pid) {
    return kill(pid, 0) == 0;
}

int sreg_register(const char *session_id) {
    char dir[NC_PATH_MAX];
    sessions_dir(dir, sizeof(dir));
    mkdir(dir, NC_DIR_MODE_PRIVATE);

    pid_t pid = getpid();
    snprintf(g_sr.pid_path, sizeof(g_sr.pid_path), "%s/%d.json", dir, pid);

    FILE *f = fopen(g_sr.pid_path, "w");
    if (!f)
        return -1;

    time_t now = nc_time_now();
    char sr_cwd[NC_PATH_MAX];
    const char *sr_dir = getcwd(sr_cwd, sizeof(sr_cwd));
    fprintf(f, "{\"pid\":%d,\"session\":\"%s\",\"started\":%ld,\"cwd\":\"%s\"}\n", pid,
            session_id ? session_id : "", (long)now, sr_dir ? sr_dir : ".");
    (void)fflush(f);
    fclose(f);
    chmod(g_sr.pid_path, NC_FILE_MODE_PRIVATE);

    g_sr.registered = true;
    return 0;
}

void sreg_unregister(void) {
    if (g_sr.registered && g_sr.pid_path[0])
        unlink(g_sr.pid_path);
    g_sr.registered = false;
}

bool sreg_is_registered(void) {
    return g_sr.registered;
}

int sreg_list_live(char **out_buf) {
    if (!out_buf)
        return 0;

    char dir[NC_PATH_MAX];
    sessions_dir(dir, sizeof(dir));

    DIR *d = opendir(dir);
    if (!d) {
        *out_buf = nc_xstrdup("(no sessions directory)");
        return 0;
    }

    char buf[SREG_MAX_SESSIONS * NC_LABEL_MAX + NC_LABEL_MAX];
    int pos = 0, count = 0;
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        const char *ext = strrchr(ent->d_name, '.');
        if (!ext || strcmp(ext, ".json") != 0)
            continue;

        pid_t pid = (pid_t)nc_safe_atoi(ent->d_name, 0);
        if (pid <= 0)
            continue;

        if (!pid_alive(pid)) {
            char stale[NC_PATH_MAX];
            snprintf(stale, sizeof(stale), "%s/%s", dir, ent->d_name);
            unlink(stale);
            continue;
        }

        char fpath[NC_PATH_MAX];
        snprintf(fpath, sizeof(fpath), "%s/%s", dir, ent->d_name);
        FILE *f = fopen(fpath, "r");
        if (!f)
            continue;
        char line[NC_MSG_MAX];
        if (fgets(line, sizeof(line), f))
            pos += snprintf(buf + pos, sizeof(buf) - pos, "  PID %d: %s", pid, line);
        fclose(f);
        count++;
        if (count >= SREG_MAX_SESSIONS)
            break;
    }
    closedir(d);

    if (count == 0)
        *out_buf = nc_xstrdup("No active sessions.");
    else
        *out_buf = nc_xstrdup(buf);
    return count;
}

int sreg_count_live(void) {
    char dir[NC_PATH_MAX];
    sessions_dir(dir, sizeof(dir));
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        pid_t pid = (pid_t)nc_safe_atoi(ent->d_name, 0);
        if (pid > 0 && pid_alive(pid))
            count++;
        else if (pid > 0) {
            char stale[NC_PATH_MAX];
            snprintf(stale, sizeof(stale), "%s/%s", dir, ent->d_name);
            unlink(stale);
        }
    }
    closedir(d);
    return count;
}
