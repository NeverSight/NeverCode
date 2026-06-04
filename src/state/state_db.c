#include "nevercode.h"

typedef struct {
    char root_dir[NC_PATH_MAX];
    bool initialized;
    int sessions_written, rollouts_written, reads;
} state_db_ctx_t;

static state_db_ctx_t g_sdb;

static void sdb_dir_path(char *out, size_t sz, const char *sub) {
    snprintf(out, sz, "%s/%s", g_sdb.root_dir, sub);
}

void state_db_init(void) {
    config_home_path(g_sdb.root_dir, sizeof(g_sdb.root_dir), "state");
    nc_mkdir_p(g_sdb.root_dir);
    char sub[NC_PATH_MAX];
    sdb_dir_path(sub, sizeof(sub), STATE_DB_DIR_SESSIONS);
    nc_mkdir_p(sub);
    sdb_dir_path(sub, sizeof(sub), STATE_DB_DIR_ROLLOUTS);
    nc_mkdir_p(sub);
    sdb_dir_path(sub, sizeof(sub), STATE_DB_DIR_MEMORIES);
    nc_mkdir_p(sub);
    sdb_dir_path(sub, sizeof(sub), STATE_DB_DIR_LOCKS);
    nc_mkdir_p(sub);
    g_sdb.sessions_written = g_sdb.rollouts_written = g_sdb.reads = 0;
    g_sdb.initialized = true;
}

void state_db_free(void) {
    g_sdb.initialized = false;
}
bool state_db_ready(void) {
    return g_sdb.initialized;
}

int state_db_save_session(const char *sid, const cJSON *data) {
    if (!g_sdb.initialized || !sid || !data)
        return -1;
    char p[NC_PATH_MAX];
    snprintf(p, sizeof(p), "%s/" STATE_DB_DIR_SESSIONS "/%s.json", g_sdb.root_dir, sid);
    int rc = nc_json_save(p, data);
    if (rc == 0)
        g_sdb.sessions_written++;
    return rc;
}

cJSON *state_db_load_session(const char *sid) {
    if (!g_sdb.initialized || !sid)
        return NULL;
    char p[NC_PATH_MAX];
    snprintf(p, sizeof(p), "%s/" STATE_DB_DIR_SESSIONS "/%s.json", g_sdb.root_dir, sid);
    g_sdb.reads++;
    return nc_json_load(p);
}

bool state_db_session_exists(const char *sid) {
    if (!g_sdb.initialized || !sid)
        return false;
    char p[NC_PATH_MAX];
    snprintf(p, sizeof(p), "%s/" STATE_DB_DIR_SESSIONS "/%s.json", g_sdb.root_dir, sid);
    return access(p, F_OK) == 0;
}

int state_db_delete_session(const char *sid) {
    if (!g_sdb.initialized || !sid)
        return -1;
    char p[NC_PATH_MAX];
    snprintf(p, sizeof(p), "%s/" STATE_DB_DIR_SESSIONS "/%s.json", g_sdb.root_dir, sid);
    return unlink(p) == 0 ? 0 : -1;
}

int state_db_list_sessions(char **ids, int max) {
    if (!g_sdb.initialized || !ids || max <= 0)
        return 0;
    char dir[NC_PATH_MAX];
    sdb_dir_path(dir, sizeof(dir), STATE_DB_DIR_SESSIONS);
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) && count < max) {
        if (str_has_suffix(ent->d_name, ".json")) {
            size_t len = strlen(ent->d_name);
            ids[count] = nc_xstrndup(ent->d_name, len - 5);
            count++;
        }
    }
    closedir(d);
    return count;
}

int state_db_save_rollout(const char *sid, int turn, const cJSON *data) {
    if (!g_sdb.initialized || !sid || !data)
        return -1;
    char p[NC_PATH_MAX];
    snprintf(p, sizeof(p), "%s/" STATE_DB_DIR_ROLLOUTS "/%s_t%d.json", g_sdb.root_dir, sid, turn);
    int rc = nc_json_save(p, data);
    if (rc == 0)
        g_sdb.rollouts_written++;
    return rc;
}

cJSON *state_db_load_rollout(const char *sid, int turn) {
    if (!g_sdb.initialized || !sid)
        return NULL;
    char p[NC_PATH_MAX];
    snprintf(p, sizeof(p), "%s/" STATE_DB_DIR_ROLLOUTS "/%s_t%d.json", g_sdb.root_dir, sid, turn);
    g_sdb.reads++;
    return nc_json_load(p);
}

int state_db_count_rollouts(const char *sid) {
    if (!g_sdb.initialized || !sid)
        return 0;
    char dir[NC_PATH_MAX];
    sdb_dir_path(dir, sizeof(dir), STATE_DB_DIR_ROLLOUTS);
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    char pfx[NC_LABEL_MAX];
    snprintf(pfx, sizeof(pfx), "%s_t", sid);
    size_t pl = strlen(pfx);
    int c = 0;
    struct dirent *ent;
    while ((ent = readdir(d)))
        if (strncmp(ent->d_name, pfx, pl) == 0)
            c++;
    closedir(d);
    return c;
}

bool state_db_try_lock(const char *name, int ttl_s) {
    if (!g_sdb.initialized || !name)
        return false;
    char p[NC_PATH_MAX];
    snprintf(p, sizeof(p), "%s/" STATE_DB_DIR_LOCKS "/%s.lock", g_sdb.root_dir, name);
    struct stat st;
    if (stat(p, &st) == 0) {
        time_t age = nc_time_now() - st.st_mtime;
        if (age < ttl_s)
            return false;
        unlink(p);
    }
    FILE *f = fopen(p, "w");
    if (!f)
        return false;
    fprintf(f, "%d", getpid());
    (void)fflush(f);
    fclose(f);
    return true;
}

void state_db_unlock(const char *name) {
    if (!g_sdb.initialized || !name)
        return;
    char p[NC_PATH_MAX];
    snprintf(p, sizeof(p), "%s/" STATE_DB_DIR_LOCKS "/%s.lock", g_sdb.root_dir, name);
    unlink(p);
}

int state_db_sessions_count(void) {
    char *ids[STATE_DB_MAX_SESSIONS];
    int n = state_db_list_sessions(ids, STATE_DB_MAX_SESSIONS);
    for (int i = 0; i < n; i++)
        free(ids[i]);
    return n;
}

int state_db_total_writes(void) {
    return g_sdb.sessions_written + g_sdb.rollouts_written;
}

char *state_db_format_stats(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "State DB: %s\n", g_sdb.root_dir);
    strbuf_appendf(&sb, "  sessions written: %d\n", g_sdb.sessions_written);
    strbuf_appendf(&sb, "  rollouts written: %d\n", g_sdb.rollouts_written);
    strbuf_appendf(&sb, "  total reads: %d\n", g_sdb.reads);
    strbuf_appendf(&sb, "  on-disk sessions: %d\n", state_db_sessions_count());
    return strbuf_detach(&sb);
}
