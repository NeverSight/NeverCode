#include "nevercode.h"

typedef struct {
    char *type;
    char *content;
    long timestamp;
} transcript_entry_t;

static struct {
    transcript_entry_t entries[TRANSCRIPT_MAX_ENTRIES];
    int count;
    bool initialized;
} g_transcript;

void transcript_init(void) {
    memset(&g_transcript, 0, sizeof(g_transcript));
    g_transcript.initialized = true;
}

void transcript_free(void) {
    for (int i = 0; i < g_transcript.count; i++) {
        free(g_transcript.entries[i].type);
        free(g_transcript.entries[i].content);
    }
    memset(&g_transcript, 0, sizeof(g_transcript));
}

int transcript_append(const char *event_type, const char *content) {
    if (!g_transcript.initialized || g_transcript.count >= TRANSCRIPT_MAX_ENTRIES)
        return -1;
    transcript_entry_t *e = &g_transcript.entries[g_transcript.count];
    e->type = event_type ? nc_xstrdup(event_type) : nc_xstrdup("unknown");
    e->content = content ? nc_xstrdup(content) : nc_xstrdup("");
    e->timestamp = nc_wall_sec();
    g_transcript.count++;
    return 0;
}

int transcript_entry_count(void) {
    return g_transcript.count;
}

char *transcript_entry_at(int idx) {
    if (idx < 0 || idx >= g_transcript.count)
        return NULL;
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "[%ld] %s: %s", g_transcript.entries[idx].timestamp,
                   g_transcript.entries[idx].type, g_transcript.entries[idx].content);
    return strbuf_detach(&sb);
}

char *transcript_format_recent(int max) {
    strbuf_t sb;
    strbuf_init(&sb);
    int start = g_transcript.count > max ? g_transcript.count - max : 0;
    strbuf_appendf(&sb, "Transcript (%d entries, showing %d):\n", g_transcript.count,
                   g_transcript.count - start);
    for (int i = start; i < g_transcript.count; i++)
        strbuf_appendf(&sb, "  [%d] %s: %.60s\n", i, g_transcript.entries[i].type,
                       g_transcript.entries[i].content);
    return strbuf_detach(&sb);
}
