/*
 * mem_citation.c — Memory provenance tracking.
 *
 * Each extracted memory records its originating session and turn,
 * enabling citation-based recall and cross-session deduplication.
 */

#include "nevercode.h"

static mem_citation_ctx_t g_citations;
static bool g_mc_init = false;

/* ── Lifecycle ── */

void mem_citation_init(mem_citation_ctx_t *c) {
    memset(c, 0, sizeof(*c));
}

void mem_citation_free(mem_citation_ctx_t *c) {
    for (int i = 0; i < c->count; i++) {
        free(c->entries[i].memory_key);
        free(c->entries[i].session_id);
        free(c->entries[i].source_text);
    }
    c->count = 0;
}

mem_citation_ctx_t *mem_citation_global(void) {
    if (!g_mc_init) {
        mem_citation_init(&g_citations);
        g_mc_init = true;
    }
    return &g_citations;
}

/* ── CRUD ── */

int mem_citation_add(mem_citation_ctx_t *c, const char *memory_key, const char *session_id,
                     int turn, const char *source_text) {
    if (!c || !memory_key || c->count >= MAX_MEM_CITATIONS)
        return -1;

    mem_citation_entry_t *e = &c->entries[c->count];
    e->memory_key = nc_xstrdup(memory_key);
    e->session_id = session_id ? nc_xstrdup(session_id) : NULL;
    e->turn = turn;
    e->source_text = source_text ? nc_xstrdup(source_text) : NULL;
    e->timestamp = nc_wall_sec();
    c->count++;
    return 0;
}

int mem_citation_find(const mem_citation_ctx_t *c, const char *memory_key,
                      const mem_citation_entry_t **out, int max) {
    if (!c || !memory_key)
        return 0;
    int f = 0;
    for (int i = 0; i < c->count && f < max; i++)
        if (strcmp(c->entries[i].memory_key, memory_key) == 0)
            out[f++] = &c->entries[i];
    return f;
}

int mem_citation_by_session(const mem_citation_ctx_t *c, const char *session_id,
                            const mem_citation_entry_t **out, int max) {
    if (!c || !session_id)
        return 0;
    int f = 0;
    for (int i = 0; i < c->count && f < max; i++)
        if (c->entries[i].session_id && strcmp(c->entries[i].session_id, session_id) == 0)
            out[f++] = &c->entries[i];
    return f;
}

int mem_citation_remove(mem_citation_ctx_t *c, const char *memory_key) {
    if (!c || !memory_key)
        return 0;
    int removed = 0, i = 0;
    while (i < c->count) {
        if (strcmp(c->entries[i].memory_key, memory_key) == 0) {
            free(c->entries[i].memory_key);
            free(c->entries[i].session_id);
            free(c->entries[i].source_text);
            memmove(&c->entries[i], &c->entries[i + 1],
                    (size_t)(c->count - i - 1) * sizeof(mem_citation_entry_t));
            c->count--;
            removed++;
        } else {
            i++;
        }
    }
    return removed;
}

/* ── Persistence ── */

int mem_citation_save(const mem_citation_ctx_t *c, const char *path) {
    if (!c || !path)
        return -1;

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < c->count; i++) {
        const mem_citation_entry_t *e = &c->entries[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "key", e->memory_key);
        if (e->session_id)
            cJSON_AddStringToObject(obj, "session", e->session_id);
        cJSON_AddNumberToObject(obj, "turn", e->turn);
        if (e->source_text)
            cJSON_AddStringToObject(obj, "source", e->source_text);
        cJSON_AddNumberToObject(obj, "ts", e->timestamp);
        cJSON_AddItemToArray(arr, obj);
    }

    char *json = nc_cjson_detach_pretty(arr);

    int rc = nc_write_file_str(path, json);
    free(json);
    return rc;
}

int mem_citation_load(mem_citation_ctx_t *c, const char *path) {
    if (!c || !path)
        return -1;

    cJSON *arr = nc_json_load(path);
    if (!arr)
        return -1;

    int loaded = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, arr) {
        const cJSON *key = cJSON_GetObjectItem(item, "key");
        if (!key || !cJSON_IsString(key))
            continue;
        if (c->count >= MAX_MEM_CITATIONS)
            break;

        mem_citation_entry_t *e = &c->entries[c->count];
        e->memory_key = nc_xstrdup(key->valuestring);

        const cJSON *s = cJSON_GetObjectItem(item, "session");
        const cJSON *t = cJSON_GetObjectItem(item, "turn");
        const cJSON *src = cJSON_GetObjectItem(item, "source");
        const cJSON *ts = cJSON_GetObjectItem(item, "ts");

        e->session_id = (s && cJSON_IsString(s)) ? nc_xstrdup(s->valuestring) : NULL;
        e->turn = (t && cJSON_IsNumber(t)) ? t->valueint : 0;
        e->source_text = (src && cJSON_IsString(src)) ? nc_xstrdup(src->valuestring) : NULL;
        e->timestamp = (ts && cJSON_IsNumber(ts)) ? (long)ts->valuedouble : 0;
        c->count++;
        loaded++;
    }
    cJSON_Delete(arr);
    return loaded;
}

/* ── Formatting ── */

char *mem_citation_format(const mem_citation_ctx_t *c) {
    if (!c || c->count == 0)
        return nc_xstrdup("No memory citations.\n");

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Memory Citations (%d):\n", c->count);

    for (int i = 0; i < c->count; i++) {
        const mem_citation_entry_t *e = &c->entries[i];
        strbuf_appendf(&sb, "  [%s]", e->memory_key);
        if (e->session_id)
            strbuf_appendf(&sb, " session=%s", e->session_id);
        if (e->turn > 0)
            strbuf_appendf(&sb, " turn=%d", e->turn);
        strbuf_append_str(&sb, "\n");
    }

    return strbuf_detach(&sb);
}

int mem_citation_count(const mem_citation_ctx_t *c) {
    return c ? c->count : 0;
}

int mem_citation_unique_sessions(const mem_citation_ctx_t *c) {
    if (!c)
        return 0;
    const char *seen[MAX_MEM_CITATIONS];
    int n = 0;
    for (int i = 0; i < c->count; i++) {
        if (!c->entries[i].session_id)
            continue;
        bool dup = false;
        for (int j = 0; j < n; j++)
            if (strcmp(seen[j], c->entries[i].session_id) == 0) {
                dup = true;
                break;
            }
        if (!dup && n < MAX_MEM_CITATIONS)
            seen[n++] = c->entries[i].session_id;
    }
    return n;
}
