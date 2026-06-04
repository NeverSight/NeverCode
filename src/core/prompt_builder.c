/*
 * prompt_builder.c — System prompt construction from layered fragments.
 *
 * Assembles the system prompt from ordered fragments: base identity,
 * project docs, user instructions, skill context,
 * and runtime directives. Supports fragment priority, dedup, and budget.
 */
#include "nevercode.h"
#include "defaults.h"

typedef enum {
    FRAG_IDENTITY = 0,
    FRAG_PROJECT_DOC,
    FRAG_USER_INSTRUCTIONS,
    FRAG_SKILL,
    FRAG_RUNTIME,
    FRAG_TOOL_HINTS,
    FRAG_SESSION_CTX,
} frag_kind_t;

typedef struct {
    char name[PB_FRAGMENT_NAME_MAX];
    char *content;
    int content_len;
    frag_kind_t kind;
    int priority;
    bool active;
} pb_fragment_t;

typedef struct {
    pb_fragment_t fragments[PB_MAX_FRAGMENTS];
    int count;
    int budget;
} pb_ctx_t;

static pb_ctx_t g_pb;

void pb_init(int budget) {
    memset(&g_pb, 0, sizeof(g_pb));
    g_pb.budget = budget > 0 ? budget : DEFAULT_MAX_TOKENS * 4;
}

void pb_cleanup(void) {
    for (int i = 0; i < g_pb.count; i++)
        free(g_pb.fragments[i].content);
    memset(&g_pb, 0, sizeof(g_pb));
}

int pb_add(const char *name, const char *content, frag_kind_t kind, int priority) {
    if (g_pb.count >= PB_MAX_FRAGMENTS || !content)
        return -1;
    for (int i = 0; i < g_pb.count; i++)
        if (g_pb.fragments[i].active && strcmp(g_pb.fragments[i].name, name) == 0)
            return -2;
    pb_fragment_t *f = &g_pb.fragments[g_pb.count];
    memset(f, 0, sizeof(*f));
    nc_strlcpy(f->name, name ? name : "", PB_FRAGMENT_NAME_MAX);
    f->content_len = (int)strlen(content);
    f->content = malloc(f->content_len + 1);
    if (!f->content)
        return -3;
    memcpy(f->content, content, f->content_len + 1);
    f->kind = kind;
    f->priority = priority;
    f->active = true;
    g_pb.count++;
    return 0;
}

int pb_remove(const char *name) {
    for (int i = 0; i < g_pb.count; i++) {
        if (g_pb.fragments[i].active && strcmp(g_pb.fragments[i].name, name) == 0) {
            g_pb.fragments[i].active = false;
            free(g_pb.fragments[i].content);
            g_pb.fragments[i].content = NULL;
            return 0;
        }
    }
    return -1;
}

static int frag_cmp(const void *a, const void *b) {
    const pb_fragment_t *fa = a, *fb = b;
    if (fa->kind != fb->kind)
        return (int)fa->kind - (int)fb->kind;
    return fb->priority - fa->priority;
}

int pb_build(char *out, int cap) {
    pb_fragment_t sorted[PB_MAX_FRAGMENTS];
    int n = 0;
    for (int i = 0; i < g_pb.count; i++)
        if (g_pb.fragments[i].active)
            sorted[n++] = g_pb.fragments[i];
    qsort(sorted, n, sizeof(pb_fragment_t), frag_cmp);

    int written = 0;
    for (int i = 0; i < n && written < cap - 1; i++) {
        int avail = cap - written - 1;
        int need = sorted[i].content_len;
        if (need > avail)
            need = avail;
        if (written > 0 && written < cap - 2) {
            out[written++] = '\n';
            out[written++] = '\n';
        }
        memcpy(out + written, sorted[i].content, need);
        written += need;
    }
    out[written] = '\0';
    return written;
}

int pb_fragment_count(void) {
    int n = 0;
    for (int i = 0; i < g_pb.count; i++)
        if (g_pb.fragments[i].active)
            n++;
    return n;
}

int pb_total_len(void) {
    int t = 0;
    for (int i = 0; i < g_pb.count; i++)
        if (g_pb.fragments[i].active)
            t += g_pb.fragments[i].content_len;
    return t;
}

bool pb_has(const char *name) {
    for (int i = 0; i < g_pb.count; i++)
        if (g_pb.fragments[i].active && strcmp(g_pb.fragments[i].name, name) == 0)
            return true;
    return false;
}
