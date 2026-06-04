/*
 * progress_tracker.c — Cross-session progress persistence
 *
 * Tracks a markdown-based progress log (pending / completed items)
 * that survives across sessions.  The file lives in the project
 * root so the model can read and update it autonomously.
 */

#include "harness.h"

typedef struct {
    char text[PROGRESS_ENTRY_MAX];
    bool done;
    long ts;
} progress_entry_t;

static struct {
    progress_entry_t entries[PROGRESS_MAX_ENTRIES];
    int count;
    char path[NC_PATH_MAX];
    bool loaded;
} g_progress;

/* ── Init / Free ── */

void progress_tracker_init(void) {
    memset(&g_progress, 0, sizeof(g_progress));
    nc_cwd_join(g_progress.path, sizeof(g_progress.path), PROGRESS_FILE);
    g_progress.loaded = false;
}

void progress_tracker_free(void) {
    memset(&g_progress, 0, sizeof(g_progress));
}

/* ── Load from file ── */

void progress_tracker_load(void) {
    if (g_progress.loaded)
        return;
    g_progress.loaded = true;

    /* Replace in-memory state from disk (avoids dupes if load runs after add+save). */
    g_progress.count = 0;

    size_t sz = 0;
    char *buf = strbuf_read_file(g_progress.path, &sz);
    if (!buf || sz == 0) {
        free(buf);
        return;
    }

    char *line = buf;
    while (line && *line && g_progress.count < PROGRESS_MAX_ENTRIES) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';

        if (line[0] == '-' && line[1] == ' ') {
            progress_entry_t *e = &g_progress.entries[g_progress.count];
            const char *text = line + 2;

            if (text[0] == '[' && text[1] == 'x' && text[2] == ']') {
                e->done = true;
                text += 4;
            } else if (text[0] == '[' && text[1] == ' ' && text[2] == ']') {
                e->done = false;
                text += 4;
            }

            nc_strlcpy(e->text, text, sizeof(e->text));
            e->ts = nc_wall_sec();
            g_progress.count++;
        }
        line = nl ? nl + 1 : NULL;
    }
    free(buf);
}

/* ── Save to file ── */

int progress_tracker_save(void) {
    time_t now = nc_time_now();
    struct tm *tm = localtime(&now);
    char tbuf[NC_FMT_MAX];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", tm);
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "# Progress Log\n\nLast updated: %s\n\n", tbuf);

    bool has_pending = false, has_done = false;
    for (int i = 0; i < g_progress.count; i++) {
        if (!g_progress.entries[i].done)
            has_pending = true;
        else
            has_done = true;
    }

    if (has_pending) {
        strbuf_append_str(&sb, "## In Progress\n\n");
        for (int i = 0; i < g_progress.count; i++)
            if (!g_progress.entries[i].done)
                strbuf_appendf(&sb, "- [ ] %s\n", g_progress.entries[i].text);
        strbuf_append_str(&sb, "\n");
    }

    if (has_done) {
        strbuf_append_str(&sb, "## Completed\n\n");
        for (int i = 0; i < g_progress.count; i++)
            if (g_progress.entries[i].done)
                strbuf_appendf(&sb, "- [x] %s\n", g_progress.entries[i].text);
        strbuf_append_str(&sb, "\n");
    }

    size_t body_len = sb.len;
    char *body = strbuf_detach(&sb);
    int rc = nc_write_file_atomic(g_progress.path, body, body_len);
    free(body);
    return rc;
}

/* ── Mutations ── */

int progress_tracker_add(const char *text, bool done) {
    if (!text || g_progress.count >= PROGRESS_MAX_ENTRIES)
        return -1;

    progress_entry_t *e = &g_progress.entries[g_progress.count++];
    nc_strlcpy(e->text, text, sizeof(e->text));
    e->done = done;
    e->ts = nc_wall_sec();
    return g_progress.count - 1;
}

int progress_tracker_mark_done(int idx) {
    if (idx < 0 || idx >= g_progress.count)
        return -1;
    g_progress.entries[idx].done = true;
    g_progress.entries[idx].ts = nc_wall_sec();
    return 0;
}

/* ── Queries ── */

int progress_tracker_count(void) {
    return g_progress.count;
}

int progress_tracker_pending_count(void) {
    int n = 0;
    for (int i = 0; i < g_progress.count; i++)
        if (!g_progress.entries[i].done)
            n++;
    return n;
}

int progress_tracker_done_count(void) {
    int n = 0;
    for (int i = 0; i < g_progress.count; i++)
        if (g_progress.entries[i].done)
            n++;
    return n;
}

/* ── Formatting ── */

char *progress_tracker_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);

    int pending = progress_tracker_pending_count();
    int done = progress_tracker_done_count();
    strbuf_appendf(&sb, "Progress: %d/%d completed", done, g_progress.count);
    if (pending > 0)
        strbuf_appendf(&sb, " (%d pending)", pending);
    strbuf_append_str(&sb, "\n");

    for (int i = 0; i < g_progress.count; i++) {
        const progress_entry_t *e = &g_progress.entries[i];
        strbuf_appendf(&sb, "  [%s] %s\n", e->done ? "x" : " ", e->text);
    }
    return strbuf_detach(&sb);
}

char *progress_tracker_context(void) {
    if (g_progress.count == 0)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "<progress_log>\n");

    for (int i = 0; i < g_progress.count; i++) {
        const progress_entry_t *e = &g_progress.entries[i];
        if (!e->done)
            strbuf_appendf(&sb, "- [ ] %s\n", e->text);
    }

    strbuf_append_str(&sb, "</progress_log>\n");
    return strbuf_detach(&sb);
}
