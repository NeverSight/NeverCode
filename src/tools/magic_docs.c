#include "nevercode.h"

#define MAGIC_HEADER_PREFIX "# MAGIC DOC:"

typedef struct {
    char path[NC_PATH_MAX];
    char title[MAGIC_DOC_TITLE_MAX];
    int last_update_turn;
    long last_update_time;
    int update_count;
    bool dirty;
} magic_doc_entry_t;

static struct {
    magic_doc_entry_t docs[MAGIC_DOC_MAX_TRACKED];
    int count;
    bool enabled;
    int total_updates;
} g_mdoc;

void magic_docs_init(void) {
    memset(&g_mdoc, 0, sizeof(g_mdoc));
    g_mdoc.enabled = true;
}
void magic_docs_free(void) {
    memset(&g_mdoc, 0, sizeof(g_mdoc));
}
void magic_docs_set_enabled(bool on) {
    g_mdoc.enabled = on;
}
bool magic_docs_enabled(void) {
    return g_mdoc.enabled;
}

bool magic_docs_detect(const char *path, const char *content) {
    if (!path || !content)
        return false;
    const char *p = content;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return nc_starts_with(p, MAGIC_HEADER_PREFIX);
}

static int find_doc(const char *path) {
    for (int i = 0; i < g_mdoc.count; i++)
        if (strcmp(g_mdoc.docs[i].path, path) == 0)
            return i;
    return -1;
}

static void extract_title(const char *content, char *title, size_t sz) {
    const char *p = strstr(content, MAGIC_HEADER_PREFIX);
    if (!p) {
        title[0] = '\0';
        return;
    }
    p += strlen(MAGIC_HEADER_PREFIX);
    while (*p == ' ')
        p++;
    const char *end = p;
    while (*end && *end != '\n' && *end != '\r')
        end++;
    size_t len = (size_t)(end - p);
    if (len >= sz)
        len = sz - 1;
    memcpy(title, p, len);
    title[len] = '\0';
}

int magic_docs_register(const char *path, const char *content) {
    if (!g_mdoc.enabled || !path || !content)
        return -1;
    if (!magic_docs_detect(path, content))
        return -1;
    if (find_doc(path) >= 0)
        return 0;
    if (g_mdoc.count >= MAGIC_DOC_MAX_TRACKED)
        return -1;
    magic_doc_entry_t *e = &g_mdoc.docs[g_mdoc.count];
    nc_strlcpy(e->path, path, sizeof(e->path));
    extract_title(content, e->title, sizeof(e->title));
    e->dirty = true;
    g_mdoc.count++;
    return 0;
}

void magic_docs_record_update(int idx) {
    if (idx < 0 || idx >= g_mdoc.count)
        return;
    magic_doc_entry_t *e = &g_mdoc.docs[idx];
    e->dirty = false;
    e->update_count++;
    e->last_update_turn = state_session_turn_number();
    e->last_update_time = nc_wall_sec();
    g_mdoc.total_updates++;
}

int magic_docs_count(void) {
    return g_mdoc.count;
}
int magic_docs_updates(void) {
    return g_mdoc.total_updates;
}
const char *magic_docs_title(int i) {
    return (i >= 0 && i < g_mdoc.count) ? g_mdoc.docs[i].title : NULL;
}

char *magic_docs_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Living Docs: %d tracked, %d total updates\n", g_mdoc.count,
                   g_mdoc.total_updates);
    for (int i = 0; i < g_mdoc.count; i++) {
        magic_doc_entry_t *e = &g_mdoc.docs[i];
        strbuf_appendf(&sb, "  [%d] \"%s\" (%s) -- %d updates%s\n", i, e->title, e->path,
                       e->update_count, e->dirty ? " [pending]" : "");
    }
    return strbuf_detach(&sb);
}
