/*
 * tool_result.c — Tool output truncation, spill-to-disk, and retention cleanup.
 *
 * When a tool produces output beyond TOOL_RESULT_MAX_CHARS the full text is
 * written to the spill directory and a head+tail preview is returned.
 * On startup, spill files older than TOOL_SPILL_RETENTION_DAYS are purged.
 */

#include "nevercode.h"

static void ensure_spill_dir(char *buf, size_t sz) {
    const char *home = nc_home_dir();
    if (!home)
        return;
    snprintf(buf, sz, "%s/%s", home, TOOL_RESULT_SPILL_DIR);
    nc_mkdir_p(buf);
}

char *tool_result_spill(const char *output, const char *tool_name) {
    char dir[NC_PATH_MAX];
    ensure_spill_dir(dir, sizeof(dir));
    if (!dir[0])
        return NULL;

    char filename[NC_PATH_MAX];
    snprintf(filename, sizeof(filename), "%s/%s_%ld_%d.txt", dir, tool_name, nc_wall_sec(),
             getpid());

    FILE *f = fopen(filename, "w");
    if (!f)
        return NULL;
    bool ok = fputs(output, f) != EOF && !fflush(f);
    fclose(f);
    return ok ? nc_xstrdup(filename) : NULL;
}

int tool_result_cleanup(void) {
    char dir[NC_PATH_MAX];
    ensure_spill_dir(dir, sizeof(dir));
    if (!dir[0])
        return 0;

    DIR *d = opendir(dir);
    if (!d)
        return 0;

    time_t cutoff = nc_time_now() - (TOOL_SPILL_RETENTION_DAYS * 86400);
    int removed = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        char path[NC_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && st.st_mtime < cutoff) {
            if (unlink(path) == 0)
                removed++;
        }
    }
    closedir(d);
    return removed;
}

char *tool_result_truncate(const char *output, const char *tool_name) {
    if (!output)
        return NULL;
    size_t len = strlen(output);
    if (len <= TOOL_RESULT_MAX_CHARS)
        return nc_xstrdup(output);

    char *spill_path = tool_result_spill(output, tool_name);

    strbuf_t sb;
    strbuf_init(&sb);

    size_t head = TOOL_RESULT_TRUNCATE_TO / 2;
    size_t tail = TOOL_RESULT_TRUNCATE_TO / 2;

    strbuf_append(&sb, output, head);
    strbuf_append_str(&sb, "\n\n--- OUTPUT TRUNCATED ---\n");

    char meta[NC_MSG_MAX];
    snprintf(meta, sizeof(meta), "(%zu chars total, showing first %zu + last %zu chars)\n", len,
             head, tail);
    strbuf_append_str(&sb, meta);

    if (spill_path) {
        strbuf_append_str(&sb, "Full output saved to: ");
        strbuf_append_str(&sb, spill_path);
        strbuf_append_str(&sb, "\n");
        free(spill_path);
    }

    strbuf_append_str(&sb, "--- END TRUNCATION ---\n\n");
    strbuf_append(&sb, output + len - tail, tail);

    return strbuf_detach(&sb);
}
