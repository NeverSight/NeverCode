/*
 * auto_dream.c — Background memory consolidation
 *
 * When the user has been idle long enough and enough sessions have
 * accumulated, spawns a background thread that loads the memory
 * store, consolidates overlapping entries, and saves the result.
 * Uses a file lock to prevent concurrent runs across processes.
 */

#include "nevercode.h"
#include "config.h"

typedef struct {
    bool enabled;
    bool running;
    long min_idle_s;
    int min_sessions;
    long last_consolidated;
    long last_scan;
    long scan_interval_s;
    int sessions_reviewed;
    int files_touched;
    int total_runs;
    char lock_path[NC_PATH_MAX];
    pthread_t thread;
    pthread_mutex_t mu;
} dream_ctx_t;

static dream_ctx_t g_dream = {0};

void auto_dream_init(void) {
    memset(&g_dream, 0, sizeof(g_dream));
    g_dream.enabled = true;
    g_dream.min_idle_s = DREAM_MIN_IDLE_S;
    g_dream.min_sessions = DREAM_MIN_SESSIONS;
    g_dream.scan_interval_s = DREAM_SCAN_INTERVAL_S;
    g_dream.last_consolidated = nc_wall_sec();
    pthread_mutex_init(&g_dream.mu, NULL);
    config_home_path(g_dream.lock_path, sizeof(g_dream.lock_path), "consolidation.lock");
}

void auto_dream_free(void) {
    pthread_mutex_destroy(&g_dream.mu);
    memset(&g_dream, 0, sizeof(g_dream));
}

void auto_dream_set_enabled(bool e) {
    g_dream.enabled = e;
}
bool auto_dream_enabled(void) {
    return g_dream.enabled;
}
bool auto_dream_running(void) {
    return g_dream.running;
}

static bool dream_lock_acquire(void) {
    if (access(g_dream.lock_path, F_OK) == 0) {
        struct stat st;
        if (stat(g_dream.lock_path, &st) == 0 &&
            nc_wall_sec() - (long)st.st_mtime < DREAM_LOCK_STALE_S)
            return false;
    }
    FILE *f = fopen(g_dream.lock_path, "w");
    if (!f)
        return false;
    fprintf(f, "%ld\n", (long)getpid());
    (void)fflush(f);
    fclose(f);
    return true;
}

static void dream_lock_release(void) {
    unlink(g_dream.lock_path);
}

static int dream_count_sessions_since(long since) {
    (void)since;
    char dir[NC_PATH_MAX];
    config_home_path(dir, sizeof(dir), "sessions");

    struct stat lock_st;
    if (stat(g_dream.lock_path, &lock_st) != 0)
        return 0;
    time_t cutoff = lock_st.st_mtime;

    DIR *d = opendir(dir);
    if (!d)
        return 0;

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t nlen = strlen(ent->d_name);
        if (nlen < 6 || strcmp(ent->d_name + nlen - 5, ".json") != 0)
            continue;
        char path[NC_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat fst;
        if (stat(path, &fst) == 0 && fst.st_mtime > cutoff)
            count++;
    }
    closedir(d);
    return count;
}

static void *dream_thread_fn(void *arg) {
    (void)arg;

    memory_ctx_t mem;
    memory_init(&mem);

    char mp[NC_PATH_MAX];
    config_home_path(mp, sizeof(mp), NC_CFG_MEMORY);
    memory_load(&mem, mp);

    int consolidated = memory_consolidate(&mem);
    if (consolidated > 0) {
        memory_save(&mem, mp);
        g_dream.files_touched += consolidated;
    }
    memory_free(&mem);

    pthread_mutex_lock(&g_dream.mu);
    g_dream.last_consolidated = nc_wall_sec();
    g_dream.sessions_reviewed += g_dream.min_sessions;
    g_dream.total_runs++;
    g_dream.running = false;
    pthread_mutex_unlock(&g_dream.mu);

    dream_lock_release();
    return NULL;
}

int auto_dream_check(void) {
    if (!g_dream.enabled || g_dream.running)
        return 0;

    long now = nc_wall_sec();
    if (now - g_dream.last_consolidated < g_dream.min_idle_s)
        return 0;
    if (now - g_dream.last_scan < g_dream.scan_interval_s)
        return 0;

    g_dream.last_scan = now;
    int sessions = dream_count_sessions_since(g_dream.last_consolidated);
    if (sessions < g_dream.min_sessions)
        return 0;
    if (!dream_lock_acquire())
        return 0;

    pthread_mutex_lock(&g_dream.mu);
    g_dream.running = true;
    pthread_mutex_unlock(&g_dream.mu);

    if (pthread_create(&g_dream.thread, NULL, dream_thread_fn, NULL) != 0) {
        g_dream.running = false;
        dream_lock_release();
        return -1;
    }
    pthread_detach(g_dream.thread);
    return 1;
}

void auto_dream_touch(void) {
    g_dream.last_consolidated = nc_wall_sec();
}

int auto_dream_total_runs(void) {
    return g_dream.total_runs;
}
int auto_dream_sessions_reviewed(void) {
    return g_dream.sessions_reviewed;
}
int auto_dream_files_touched(void) {
    return g_dream.files_touched;
}

char *auto_dream_status(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    long since = nc_wall_sec() - g_dream.last_consolidated;
    strbuf_appendf(&sb, "Background Consolidation: %s\n", g_dream.enabled ? "enabled" : "disabled");
    strbuf_appendf(&sb, "  Status: %s\n", g_dream.running ? "running" : "idle");
    strbuf_appendf(&sb, "  Last run: %lds ago\n", since);
    strbuf_appendf(&sb, "  Total runs: %d\n", g_dream.total_runs);
    strbuf_appendf(&sb, "  Sessions reviewed: %d\n", g_dream.sessions_reviewed);
    strbuf_appendf(&sb, "  Files touched: %d\n", g_dream.files_touched);

    return strbuf_detach(&sb);
}
