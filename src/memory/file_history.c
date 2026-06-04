#include "nevercode.h"

#define MAX_SNAPSHOTS MAX_FILE_SNAPSHOTS
#define FH_PATH_MAX NC_PATH_MAX
#define MAX_READ_BYTES DIFF_BASELINE_FILE_MAX

typedef enum {
    FILE_SNAPSHOT_MISSING = 0,
    FILE_SNAPSHOT_FILE,
    FILE_SNAPSHOT_DIR
} file_snapshot_kind_t;

typedef struct {
    char path[FH_PATH_MAX];
    char anchor_dir[FH_PATH_MAX];
    char *content;
    size_t content_len;
    time_t timestamp;
    int turn;
    file_snapshot_kind_t kind;
} file_snapshot_t;

static file_snapshot_t g_snapshots[MAX_SNAPSHOTS];
static int g_num_snapshots = 0;
static int g_current_turn = 0;

static void parent_dir_of(const char *path, char *out, size_t cap) {
    if (!path || !*path) {
        nc_strlcpy(out, ".", cap);
        return;
    }
    char tmp[FH_PATH_MAX];
    nc_strlcpy(tmp, path, sizeof(tmp));
    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';
    char *slash = strrchr(tmp, '/');
    if (!slash) {
        nc_strlcpy(out, ".", cap);
        return;
    }
    if (slash == tmp) {
        nc_strlcpy(out, "/", cap);
        return;
    }
    *slash = '\0';
    nc_strlcpy(out, tmp, cap);
}

static void snapshot_anchor_dir(const char *path, char *out, size_t cap) {
    char cur[FH_PATH_MAX];
    parent_dir_of(path, cur, sizeof(cur));
    for (;;) {
        struct stat st;
        if (stat(cur, &st) == 0 && S_ISDIR(st.st_mode)) {
            nc_strlcpy(out, cur, cap);
            return;
        }
        if (strcmp(cur, ".") == 0 || strcmp(cur, "/") == 0) {
            nc_strlcpy(out, cur, cap);
            return;
        }
        char parent[FH_PATH_MAX];
        parent_dir_of(cur, parent, sizeof(parent));
        if (strcmp(parent, cur) == 0) {
            nc_strlcpy(out, ".", cap);
            return;
        }
        nc_strlcpy(cur, parent, sizeof(cur));
    }
}

static int ensure_parent_dirs(const char *path) {
    char parent[FH_PATH_MAX];
    parent_dir_of(path, parent, sizeof(parent));
    if (strcmp(parent, ".") == 0 || strcmp(parent, "/") == 0)
        return 0;
    return nc_mkdir_p(parent);
}

static void prune_empty_parent_dirs(const char *path, const char *anchor_dir) {
    char cur[FH_PATH_MAX];
    parent_dir_of(path, cur, sizeof(cur));
    while (strcmp(cur, ".") != 0 && strcmp(cur, "/") != 0 &&
           (!anchor_dir || strcmp(cur, anchor_dir) != 0)) {
        if (rmdir(cur) != 0)
            break;
        char parent[FH_PATH_MAX];
        parent_dir_of(cur, parent, sizeof(parent));
        if (strcmp(parent, cur) == 0)
            break;
        nc_strlcpy(cur, parent, sizeof(cur));
    }
}

void file_history_init(void) {
    g_num_snapshots = 0;
    g_current_turn = 0;
}

void file_history_free(void) {
    for (int i = 0; i < g_num_snapshots; i++)
        free(g_snapshots[i].content);
    g_num_snapshots = 0;
}

void file_history_next_turn(void) {
    g_current_turn++;
}

int file_history_current_turn(void) {
    return g_current_turn;
}

int file_history_count(void) {
    return g_num_snapshots;
}

bool file_history_has_turn(int turn) {
    if (turn <= 0)
        return false;
    for (int i = 0; i < g_num_snapshots; i++)
        if (g_snapshots[i].turn == turn)
            return true;
    return false;
}

static bool already_snapped_this_turn(const char *path) {
    for (int i = g_num_snapshots - 1; i >= 0; i--) {
        if (g_snapshots[i].turn != g_current_turn)
            break;
        if (strcmp(g_snapshots[i].path, path) == 0)
            return true;
    }
    return false;
}

int file_history_snapshot(const char *path);

static int append_snapshot_base(const char *path, file_snapshot_kind_t kind) {
    if (!path || g_num_snapshots >= MAX_SNAPSHOTS)
        return -1;
    if (already_snapped_this_turn(path))
        return 0;

    file_snapshot_t *snap = &g_snapshots[g_num_snapshots];
    memset(snap, 0, sizeof(*snap));
    nc_strlcpy(snap->path, path, FH_PATH_MAX);
    snapshot_anchor_dir(path, snap->anchor_dir, sizeof(snap->anchor_dir));
    snap->timestamp = nc_time_now();
    snap->turn = g_current_turn;
    snap->kind = kind;
    g_num_snapshots++;
    return 0;
}

static int append_file_snapshot(const char *path, const struct stat *st) {
    if (append_snapshot_base(path, FILE_SNAPSHOT_FILE) != 0)
        return -1;

    file_snapshot_t *snap = &g_snapshots[g_num_snapshots - 1];
    if ((size_t)st->st_size > MAX_READ_BYTES) {
        snap->content = NULL;
        snap->content_len = 0;
        return 0;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        snap->content = NULL;
        snap->content_len = 0;
        return 0;
    }

    snap->content = malloc((size_t)st->st_size + 1u);
    if (!snap->content) {
        snap->content_len = 0;
        fclose(f);
        return 0;
    }

    size_t nread = fread(snap->content, 1, (size_t)st->st_size, f);
    fclose(f);
    if (nread != (size_t)st->st_size) {
        free(snap->content);
        snap->content = NULL;
        snap->content_len = 0;
        return 0;
    }

    snap->content_len = nread;
    snap->content[nread] = '\0';
    return 0;
}

static int snapshot_directory_tree(const char *path) {
    if (append_snapshot_base(path, FILE_SNAPSHOT_DIR) != 0)
        return -1;

    DIR *dir = opendir(path);
    if (!dir)
        return -1;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        char child[FH_PATH_MAX];
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        if (file_history_snapshot(child) != 0) {
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
    return 0;
}

int file_history_snapshot(const char *path) {
    if (!path || g_num_snapshots >= MAX_SNAPSHOTS)
        return -1;
    if (already_snapped_this_turn(path))
        return 0;

    struct stat st;
    if (stat(path, &st) != 0)
        return append_snapshot_base(path, FILE_SNAPSHOT_MISSING);
    if (S_ISDIR(st.st_mode))
        return snapshot_directory_tree(path);
    if (!S_ISREG(st.st_mode))
        return append_snapshot_base(path, FILE_SNAPSHOT_MISSING);
    return append_file_snapshot(path, &st);
}

int file_history_unique_turns(int *turns, int max) {
    int count = 0;
    int last = -1;
    for (int i = 0; i < g_num_snapshots && count < max; i++) {
        if (g_snapshots[i].turn != last) {
            turns[count++] = g_snapshots[i].turn;
            last = g_snapshots[i].turn;
        }
    }
    return count;
}

int file_history_files_at_turn(int turn, const char **paths, int max) {
    int count = 0;
    for (int i = 0; i < g_num_snapshots && count < max; i++) {
        if (g_snapshots[i].turn == turn) {
            bool dup = false;
            for (int j = 0; j < count; j++)
                if (strcmp(paths[j], g_snapshots[i].path) == 0) {
                    dup = true;
                    break;
                }
            if (!dup)
                paths[count++] = g_snapshots[i].path;
        }
    }
    return count;
}

static const file_snapshot_t *find_snapshot(int turn, const char *path) {
    const file_snapshot_t *best = NULL;
    for (int i = 0; i < g_num_snapshots; i++) {
        if (g_snapshots[i].turn == turn && strcmp(g_snapshots[i].path, path) == 0)
            best = &g_snapshots[i];
    }
    return best;
}

int file_history_restore_turn(int turn, int *restored, int *errors) {
    int r = 0, e = 0;
    const char *paths[FILE_HISTORY_PATHS_BATCH_MAX];
    int n = file_history_files_at_turn(turn, paths, FILE_HISTORY_PATHS_BATCH_MAX);

    for (int i = 0; i < n; i++) {
        const file_snapshot_t *snap = find_snapshot(turn, paths[i]);
        if (!snap) {
            e++;
            continue;
        }

        if (snap->kind == FILE_SNAPSHOT_MISSING) {
            if (nc_rm_rf(paths[i]) == 0) {
                prune_empty_parent_dirs(paths[i], snap->anchor_dir);
                r++;
            } else
                e++;
        } else if (snap->kind == FILE_SNAPSHOT_DIR) {
            struct stat cur_st;
            if (lstat(paths[i], &cur_st) == 0) {
                if (!S_ISDIR(cur_st.st_mode) || S_ISLNK(cur_st.st_mode)) {
                    if (nc_rm_rf(paths[i]) != 0) {
                        e++;
                        continue;
                    }
                } else {
                    r++;
                    continue;
                }
            }
            if (ensure_parent_dirs(paths[i]) != 0) {
                e++;
                continue;
            }
            if (mkdir(paths[i], NC_DIR_MODE) == 0 || errno == EEXIST)
                r++;
            else
                e++;
        } else if (snap->kind == FILE_SNAPSHOT_FILE && snap->content) {
            struct stat cur_st;
            if (lstat(paths[i], &cur_st) == 0 && S_ISDIR(cur_st.st_mode) &&
                nc_rm_rf(paths[i]) != 0) {
                e++;
                continue;
            }
            if (ensure_parent_dirs(paths[i]) != 0) {
                e++;
                continue;
            }
            FILE *f = fopen(paths[i], "wb");
            if (f) {
                size_t w = fwrite(snap->content, 1, snap->content_len, f);
                int fe = fflush(f);
                fclose(f);
                if (w < snap->content_len || fe)
                    e++;
                else
                    r++;
            } else {
                e++;
            }
        } else {
            e++;
        }
    }

    if (restored)
        *restored = r;
    if (errors)
        *errors = e;
    return r;
}

char *file_history_format_checkpoints(void) {
    if (g_num_snapshots == 0)
        return nc_xstrdup("(no file changes recorded)\n");

    strbuf_t sb;
    strbuf_init(&sb);

    int turns[FILE_HISTORY_PATHS_BATCH_MAX];
    int nt = file_history_unique_turns(turns, FILE_HISTORY_PATHS_BATCH_MAX);

    for (int i = nt - 1; i >= 0; i--) {
        char header[NC_TAG_MAX];
        const file_snapshot_t *first = NULL;
        for (int j = 0; j < g_num_snapshots; j++) {
            if (g_snapshots[j].turn == turns[i]) {
                first = &g_snapshots[j];
                break;
            }
        }
        struct tm tmbuf;
        struct tm *t = first ? localtime_r(&first->timestamp, &tmbuf) : NULL;
        if (t) {
            snprintf(header, sizeof(header), "Turn %d (%02d:%02d:%02d):\n", turns[i], t->tm_hour,
                     t->tm_min, t->tm_sec);
        } else {
            snprintf(header, sizeof(header), "Turn %d:\n", turns[i]);
        }
        strbuf_append_str(&sb, header);

        const char *paths[FILE_HISTORY_PATHS_BATCH_MAX];
        int np = file_history_files_at_turn(turns[i], paths, FILE_HISTORY_PATHS_BATCH_MAX);
        for (int j = 0; j < np; j++) {
            const file_snapshot_t *s = find_snapshot(turns[i], paths[j]);
            char line[NC_PATH_MAX + 128];
            if (s && s->kind == FILE_SNAPSHOT_MISSING) {
                snprintf(line, sizeof(line), "  [new] %s\n", paths[j]);
            } else if (s && s->kind == FILE_SNAPSHOT_DIR) {
                snprintf(line, sizeof(line), "  [dir] %s\n", paths[j]);
            } else if (s && s->kind == FILE_SNAPSHOT_FILE && s->content) {
                snprintf(line, sizeof(line), "  [mod] %s (%zu bytes)\n", paths[j], s->content_len);
            } else {
                snprintf(line, sizeof(line), "  [mod] %s\n", paths[j]);
            }
            strbuf_append_str(&sb, line);
        }
    }

    return strbuf_detach(&sb);
}
