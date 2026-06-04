/*
 * turn_diff.c — File change tracker with baseline snapshots
 *
 * Records a baseline snapshot of each file the first time it is modified.
 * Subsequent queries produce real unified diffs by comparing baselines
 * against the current on-disk content, with proper context lines, file
 * mode awareness, binary detection, and rename tracking.
 */

#include "nevercode.h"

/* ── File mode enum ── */

typedef enum {
    FMODE_REGULAR = 0,
    FMODE_EXEC,
    FMODE_SYMLINK,
} fmode_t;

static const char *fmode_str(fmode_t m) {
    switch (m) {
    case FMODE_EXEC:
        return "100755";
    case FMODE_SYMLINK:
        return "120000";
    default:
        return "100644";
    }
}

/* ── Stat-level entry (lightweight +/- counters) ── */

typedef struct {
    int turn;
    char *path;
    int lines_added;
    int lines_removed;
} turn_diff_entry_t;

static turn_diff_entry_t g_entries[MAX_TURN_DIFFS];
static int g_count = 0;

/* ── Baseline snapshot store ── */

typedef struct {
    char *path;
    char *current_path;
    char *content;
    size_t content_len;
    fmode_t mode;
    int first_turn;
} baseline_t;

static baseline_t g_baselines[DIFF_BASELINE_MAX];
static int g_bl_count = 0;

/* ── Helpers ── */

static fmode_t detect_file_mode(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0)
        return FMODE_REGULAR;
    if (S_ISLNK(st.st_mode))
        return FMODE_SYMLINK;
#ifdef S_IXUSR
    if (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
        return FMODE_EXEC;
#endif
    return FMODE_REGULAR;
}

static bool is_binary_data(const char *data, size_t len) {
    size_t scan = len < DIFF_BINARY_SCAN_BYTES ? len : DIFF_BINARY_SCAN_BYTES;
    for (size_t i = 0; i < scan; i++) {
        if (data[i] == '\0')
            return true;
    }
    return false;
}

static char *read_file_contents(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        if (out_len)
            *out_len = 0;
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    if (sz < 0 || sz > (long)DIFF_BASELINE_FILE_MAX) {
        fclose(fp);
        if (out_len)
            *out_len = 0;
        return NULL;
    }
    fseek(fp, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        if (out_len)
            *out_len = 0;
        return NULL;
    }
    size_t nr = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[nr] = '\0';
    if (out_len)
        *out_len = nr;
    return buf;
}

/* ── Init / Free ── */

void turn_diff_init(void) {
    g_count = 0;
    memset(g_entries, 0, sizeof(g_entries));
    g_bl_count = 0;
    memset(g_baselines, 0, sizeof(g_baselines));
}

void turn_diff_free(void) {
    for (int i = 0; i < g_count; i++)
        free(g_entries[i].path);
    g_count = 0;

    for (int i = 0; i < g_bl_count; i++) {
        free(g_baselines[i].path);
        free(g_baselines[i].current_path);
        free(g_baselines[i].content);
    }
    g_bl_count = 0;
}

/* ── Baseline capture ── */

static baseline_t *find_baseline(const char *path) {
    for (int i = 0; i < g_bl_count; i++) {
        const char *p =
            g_baselines[i].current_path ? g_baselines[i].current_path : g_baselines[i].path;
        if (strcmp(p, path) == 0)
            return &g_baselines[i];
    }
    for (int i = 0; i < g_bl_count; i++) {
        if (strcmp(g_baselines[i].path, path) == 0)
            return &g_baselines[i];
    }
    return NULL;
}

void turn_diff_snapshot_baseline(const char *path, int turn) {
    if (!path || find_baseline(path))
        return;
    if (g_bl_count >= DIFF_BASELINE_MAX)
        return;

    baseline_t *bl = &g_baselines[g_bl_count];
    bl->path = nc_xstrdup(path);
    bl->current_path = NULL;
    bl->first_turn = turn;

    struct stat st;
    if (stat(path, &st) == 0 && (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))) {
        bl->content = read_file_contents(path, &bl->content_len);
        bl->mode = detect_file_mode(path);
    } else {
        bl->content = NULL;
        bl->content_len = 0;
        bl->mode = FMODE_REGULAR;
    }
    g_bl_count++;
}

/* ── Rename tracking ── */

void turn_diff_track_rename(const char *old_path, const char *new_path) {
    if (!old_path || !new_path)
        return;
    baseline_t *bl = find_baseline(old_path);
    if (!bl)
        return;
    free(bl->current_path);
    bl->current_path = nc_xstrdup(new_path);
}

/* ── Recording ── */

void turn_diff_record(int turn, const char *path, int lines_added, int lines_removed) {
    if (!path || g_count >= MAX_TURN_DIFFS)
        return;

    turn_diff_snapshot_baseline(path, turn);

    for (int i = 0; i < g_count; i++) {
        if (g_entries[i].turn == turn && strcmp(g_entries[i].path, path) == 0) {
            g_entries[i].lines_added += lines_added;
            g_entries[i].lines_removed += lines_removed;
            return;
        }
    }

    g_entries[g_count].turn = turn;
    g_entries[g_count].path = nc_xstrdup(path);
    g_entries[g_count].lines_added = lines_added;
    g_entries[g_count].lines_removed = lines_removed;
    g_count++;
}

/* ── Unified diff generation ── */

typedef struct {
    const char **lines;
    int count;
} line_arr_t;

static line_arr_t split_lines(const char *text, size_t len) {
    line_arr_t arr = {NULL, 0};
    if (!text || len == 0)
        return arr;

    int cap = 64;
    arr.lines = (const char **)malloc(sizeof(char *) * (size_t)cap);
    if (!arr.lines)
        return arr;
    const char *p = text;
    const char *end = text + len;

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        if (!nl)
            nl = end;
        if (arr.count >= cap) {
            cap *= 2;
            const char **tmp =
                (const char **)realloc((void *)arr.lines, sizeof(char *) * (size_t)cap);
            if (!tmp)
                break;
            arr.lines = tmp;
        }
        size_t llen = (size_t)(nl - p);
        char *line = malloc(llen + 1);
        if (!line)
            break;
        memcpy(line, p, llen);
        line[llen] = '\0';
        arr.lines[arr.count++] = line;
        p = nl < end ? nl + 1 : end;
    }
    return arr;
}

static void free_line_arr(line_arr_t *arr) {
    for (int i = 0; i < arr->count; i++)
        free((void *)arr->lines[i]);
    free(arr->lines);
    arr->lines = NULL;
    arr->count = 0;
}

static void emit_hunk_ctx(strbuf_t *sb, const line_arr_t *old_arr, const line_arr_t *new_arr,
                          int old_start, int old_end, int new_start, int new_end,
                          const int *match_old, const int *match_new, int m, int n) {
    int ctx = DIFF_CONTEXT_LINES;
    int ctx_os = NC_MAX(old_start - ctx, 0);
    int ctx_oe = NC_MIN(old_end + ctx, m);
    int ctx_ns = NC_MAX(new_start - ctx, 0);
    int ctx_ne = NC_MIN(new_end + ctx, n);

    int hunk_old_count = ctx_oe - ctx_os;
    int hunk_new_count = ctx_ne - ctx_ns;

    char hdr[NC_TAG_MAX];
    snprintf(hdr, sizeof(hdr), "@@ -%d,%d +%d,%d @@\n", ctx_os + 1, hunk_old_count, ctx_ns + 1,
             hunk_new_count);
    strbuf_append_str(sb, hdr);

    for (int i = ctx_os; i < old_start; i++) {
        strbuf_append_str(sb, " ");
        strbuf_append_str(sb, old_arr->lines[i]);
        strbuf_append_str(sb, "\n");
    }

    int oi = old_start, ni = new_start;
    while (oi < old_end || ni < new_end) {
        if (oi < old_end && ni < new_end && match_old[oi] == ni && match_new[ni] == oi) {
            strbuf_append_str(sb, " ");
            strbuf_append_str(sb, old_arr->lines[oi]);
            strbuf_append_str(sb, "\n");
            oi++;
            ni++;
        } else if (oi < old_end && (oi >= m || match_old[oi] < 0 || match_old[oi] >= new_end)) {
            strbuf_append_str(sb, "-");
            strbuf_append_str(sb, old_arr->lines[oi]);
            strbuf_append_str(sb, "\n");
            oi++;
        } else if (ni < new_end) {
            strbuf_append_str(sb, "+");
            strbuf_append_str(sb, new_arr->lines[ni]);
            strbuf_append_str(sb, "\n");
            ni++;
        } else {
            oi++;
        }
    }

    for (int i = old_end; i < ctx_oe; i++) {
        strbuf_append_str(sb, " ");
        strbuf_append_str(sb, old_arr->lines[i]);
        strbuf_append_str(sb, "\n");
    }
}

static void emit_hunk_simple(strbuf_t *sb, const line_arr_t *old_arr, const line_arr_t *new_arr,
                             int os, int oc, int ns, int nc) {
    char hdr[NC_TAG_MAX];
    snprintf(hdr, sizeof(hdr), "@@ -%d,%d +%d,%d @@\n", os + 1, oc, ns + 1, nc);
    strbuf_append_str(sb, hdr);
    for (int i = os; i < os + oc; i++) {
        strbuf_append_str(sb, "-");
        strbuf_append_str(sb, old_arr->lines[i]);
        strbuf_append_str(sb, "\n");
    }
    for (int i = ns; i < ns + nc; i++) {
        strbuf_append_str(sb, "+");
        strbuf_append_str(sb, new_arr->lines[i]);
        strbuf_append_str(sb, "\n");
    }
}

static char *compute_unified_diff(const char *old_path, const char *new_path, const char *old_text,
                                  size_t old_len, const char *new_text, size_t new_len,
                                  fmode_t old_mode, fmode_t new_mode, bool is_new, bool is_del) {
    strbuf_t sb;
    strbuf_init(&sb);

    char hdr[NC_PATH_MAX * 2 + 64];
    snprintf(hdr, sizeof(hdr), "diff --git a/%s b/%s\n", old_path, new_path);
    strbuf_append_str(&sb, hdr);

    if (is_new)
        strbuf_appendf(&sb, "new file mode %s\n", fmode_str(new_mode));
    else if (is_del)
        strbuf_appendf(&sb, "deleted file mode %s\n", fmode_str(old_mode));
    else if (old_mode != new_mode) {
        strbuf_appendf(&sb, "old mode %s\n", fmode_str(old_mode));
        strbuf_appendf(&sb, "new mode %s\n", fmode_str(new_mode));
    }

    if (strcmp(old_path, new_path) != 0) {
        strbuf_appendf(&sb, "rename from %s\n", old_path);
        strbuf_appendf(&sb, "rename to %s\n", new_path);
    }

    bool old_bin = old_text && is_binary_data(old_text, old_len);
    bool new_bin = new_text && is_binary_data(new_text, new_len);
    if (old_bin || new_bin) {
        if (is_new)
            strbuf_appendf(&sb, "--- /dev/null\n+++ b/%s\n", new_path);
        else if (is_del)
            strbuf_appendf(&sb, "--- a/%s\n+++ /dev/null\n", old_path);
        else
            strbuf_appendf(&sb, "--- a/%s\n+++ b/%s\n", old_path, new_path);
        strbuf_append_str(&sb, "Binary files differ\n");
        return strbuf_detach(&sb);
    }

    if (is_new)
        strbuf_appendf(&sb, "--- /dev/null\n+++ b/%s\n", new_path);
    else if (is_del)
        strbuf_appendf(&sb, "--- a/%s\n+++ /dev/null\n", old_path);
    else
        strbuf_appendf(&sb, "--- a/%s\n+++ b/%s\n", old_path, new_path);

    line_arr_t old_lines = split_lines(old_text, old_len);
    line_arr_t new_lines = split_lines(new_text, new_len);
    int m = old_lines.count, n = new_lines.count;

    if ((long)m * n > (long)DIFF_LCS_LINE_LIMIT * DIFF_LCS_LINE_LIMIT) {
        emit_hunk_simple(&sb, &old_lines, &new_lines, 0, m, 0, n);
        free_line_arr(&old_lines);
        free_line_arr(&new_lines);
        return strbuf_detach(&sb);
    }

    int *lcs = calloc((size_t)(m + 1) * (size_t)(n + 1), sizeof(int));
    if (!lcs) {
        free_line_arr(&old_lines);
        free_line_arr(&new_lines);
        return nc_xstrdup("");
    }
    for (int i = m - 1; i >= 0; i--) {
        for (int j = n - 1; j >= 0; j--) {
            if (strcmp(old_lines.lines[i], new_lines.lines[j]) == 0)
                lcs[i * (n + 1) + j] = lcs[(i + 1) * (n + 1) + (j + 1)] + 1;
            else
                lcs[i * (n + 1) + j] =
                    NC_MAX(lcs[(i + 1) * (n + 1) + j], lcs[i * (n + 1) + (j + 1)]);
        }
    }

    int *match_old = malloc(sizeof(int) * (size_t)(m > 0 ? m : 1));
    int *match_new = malloc(sizeof(int) * (size_t)(n > 0 ? n : 1));
    if (!match_old || !match_new) {
        free(lcs);
        free(match_old);
        free(match_new);
        free_line_arr(&old_lines);
        free_line_arr(&new_lines);
        return nc_xstrdup("");
    }
    for (int i = 0; i < m; i++)
        match_old[i] = -1;
    for (int j = 0; j < n; j++)
        match_new[j] = -1;

    {
        int i = 0, j = 0;
        while (i < m && j < n) {
            if (strcmp(old_lines.lines[i], new_lines.lines[j]) == 0) {
                match_old[i] = j;
                match_new[j] = i;
                i++;
                j++;
            } else if (lcs[(i + 1) * (n + 1) + j] >= lcs[i * (n + 1) + (j + 1)]) {
                i++;
            } else {
                j++;
            }
        }
    }
    free(lcs);

    typedef struct {
        int os, oe, ns, ne;
    } hunk_range_t;
    hunk_range_t hunks[MAX_TURN_DIFFS];
    int nhunks = 0;

    int oi = 0, ni = 0;
    while (oi < m || ni < n) {
        if (oi < m && ni < n && match_old[oi] == ni) {
            oi++;
            ni++;
            continue;
        }
        int h_os = oi, h_ns = ni;
        while (oi < m || ni < n) {
            if (oi < m && ni < n && match_old[oi] == ni) {
                int ctx = 0, ti = oi, tj = ni;
                while (ti < m && tj < n && match_old[ti] == tj) {
                    ctx++;
                    ti++;
                    tj++;
                    if (ctx > DIFF_CONTEXT_LINES * 2)
                        break;
                }
                if (ctx > DIFF_CONTEXT_LINES * 2)
                    break;
                oi = ti;
                ni = tj;
            } else if (oi < m && match_old[oi] < 0) {
                oi++;
            } else if (ni < n && match_new[ni] < 0) {
                ni++;
            } else if (oi < m) {
                oi++;
            } else {
                ni++;
            }
        }
        if ((oi > h_os || ni > h_ns) && nhunks < MAX_TURN_DIFFS) {
            hunks[nhunks++] = (hunk_range_t){h_os, oi, h_ns, ni};
        }
    }

    for (int h = 0; h < nhunks; h++) {
        emit_hunk_ctx(&sb, &old_lines, &new_lines, hunks[h].os, hunks[h].oe, hunks[h].ns,
                      hunks[h].ne, match_old, match_new, m, n);
    }

    free(match_old);
    free(match_new);
    free_line_arr(&old_lines);
    free_line_arr(&new_lines);

    return strbuf_detach(&sb);
}

/* ── Public API ── */

char *turn_diff_unified(const char *path) {
    if (!path)
        return nc_xstrdup("");

    baseline_t *bl = find_baseline(path);
    if (!bl)
        return nc_xstrdup("");

    const char *old_path = bl->path;
    const char *new_path = bl->current_path ? bl->current_path : bl->path;

    size_t cur_len = 0;
    char *cur_text = read_file_contents(new_path, &cur_len);
    fmode_t cur_mode = detect_file_mode(new_path);

    const char *old_text = bl->content ? bl->content : "";
    size_t old_len = bl->content_len;
    fmode_t old_mode = bl->mode;

    bool is_new = (old_len == 0 && bl->content == NULL);
    bool is_del = (cur_text == NULL && old_len > 0);

    if (!cur_text && old_len == 0)
        return nc_xstrdup("");

    if (!is_new && !is_del && cur_text && old_len == cur_len &&
        memcmp(old_text, cur_text, old_len) == 0 && old_mode == cur_mode) {
        free(cur_text);
        return nc_xstrdup("");
    }

    const char *nt = cur_text ? cur_text : "";
    size_t nl = cur_text ? cur_len : 0;

    char *diff = compute_unified_diff(old_path, new_path, is_new ? NULL : old_text,
                                      is_new ? 0 : old_len, is_del ? NULL : nt, is_del ? 0 : nl,
                                      old_mode, cur_mode, is_new, is_del);
    free(cur_text);
    return diff;
}

char *turn_diff_unified_all(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    int found = 0;

    for (int i = 0; i < g_bl_count; i++) {
        const char *p =
            g_baselines[i].current_path ? g_baselines[i].current_path : g_baselines[i].path;
        char *d = turn_diff_unified(p);
        if (d && *d) {
            strbuf_append_str(&sb, d);
            if (sb.len > 0 && sb.data[sb.len - 1] != '\n')
                strbuf_append_str(&sb, "\n");
            found++;
        }
        free(d);
    }

    if (!found) {
        strbuf_free(&sb);
        return nc_xstrdup("No file changes detected.\n");
    }
    return strbuf_detach(&sb);
}

/* ── Stat formatters ── */

char *turn_diff_format(int turn) {
    strbuf_t sb;
    strbuf_init(&sb);
    int found = 0;

    for (int i = 0; i < g_count; i++) {
        if (g_entries[i].turn != turn)
            continue;
        found++;
        char line[NC_MSG_MAX];
        snprintf(line, sizeof(line), "  %s  +%d -%d\n", g_entries[i].path, g_entries[i].lines_added,
                 g_entries[i].lines_removed);
        strbuf_append_str(&sb, line);
    }

    if (!found) {
        strbuf_free(&sb);
        return nc_xstrdup("(no file changes this turn)\n");
    }
    return strbuf_detach(&sb);
}

char *turn_diff_format_all(void) {
    if (g_count == 0)
        return nc_xstrdup("No file changes recorded.\n");

    strbuf_t sb;
    strbuf_init(&sb);

    int turns[CTX_MAX_MSG_GROUPS];
    int num_turns = 0;

    for (int i = 0; i < g_count; i++) {
        int t = g_entries[i].turn;
        int dup = 0;
        for (int j = 0; j < num_turns; j++) {
            if (turns[j] == t) {
                dup = 1;
                break;
            }
        }
        if (!dup && num_turns < NC_TURN_DIFF_MAX_TURNS)
            turns[num_turns++] = t;
    }

    for (int i = 0; i < num_turns - 1; i++)
        for (int j = i + 1; j < num_turns; j++)
            if (turns[i] > turns[j]) {
                int tmp = turns[i];
                turns[i] = turns[j];
                turns[j] = tmp;
            }

    int total_add = 0, total_del = 0;
    for (int ti = 0; ti < num_turns; ti++) {
        char header[NC_FMT_MAX];
        snprintf(header, sizeof(header), "Turn %d:\n", turns[ti]);
        strbuf_append_str(&sb, header);

        for (int i = 0; i < g_count; i++) {
            if (g_entries[i].turn != turns[ti])
                continue;
            char line[NC_MSG_MAX];
            snprintf(line, sizeof(line), "  %s  +%d -%d\n", g_entries[i].path,
                     g_entries[i].lines_added, g_entries[i].lines_removed);
            strbuf_append_str(&sb, line);
            total_add += g_entries[i].lines_added;
            total_del += g_entries[i].lines_removed;
        }
    }

    char summary[NC_TAG_MAX];
    snprintf(summary, sizeof(summary), "\nTotal: +%d -%d across %d turn(s)\n", total_add, total_del,
             num_turns);
    strbuf_append_str(&sb, summary);

    return strbuf_detach(&sb);
}

int turn_diff_total_added(void) {
    int total = 0;
    for (int i = 0; i < g_count; i++)
        total += g_entries[i].lines_added;
    return total;
}

int turn_diff_total_removed(void) {
    int total = 0;
    for (int i = 0; i < g_count; i++)
        total += g_entries[i].lines_removed;
    return total;
}

int turn_diff_baseline_count(void) {
    return g_bl_count;
}

int turn_diff_files_changed(void) {
    int n = 0;
    for (int i = 0; i < g_bl_count; i++) {
        const char *p =
            g_baselines[i].current_path ? g_baselines[i].current_path : g_baselines[i].path;
        size_t cur_len = 0;
        char *cur = read_file_contents(p, &cur_len);
        bool changed = false;
        if (!cur && g_baselines[i].content_len > 0)
            changed = true;
        else if (cur && (!g_baselines[i].content || cur_len != g_baselines[i].content_len ||
                         memcmp(cur, g_baselines[i].content, cur_len) != 0))
            changed = true;
        free(cur);
        if (changed)
            n++;
    }
    return n;
}
