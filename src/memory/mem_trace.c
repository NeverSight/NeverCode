/*
 * mem_trace.c — Build structured memories from trace/log files.
 *
 * Parses JSON or JSONL trace files, normalizes items, and extracts
 * readable summaries for the memory system.
 */

#include "nevercode.h"

static bool is_allowed_role(const char *role) {
    if (!role)
        return false;
    return (strcmp(role, "assistant") == 0 || strcmp(role, "user") == 0 ||
            strcmp(role, "system") == 0 || strcmp(role, "developer") == 0);
}

static char *load_trace_text(const char *path) {
    size_t n = 0;
    char *buf = strbuf_read_file(path, &n);
    if (!buf)
        return NULL;
    /* Strip UTF-8 BOM if present */
    unsigned char *u = (unsigned char *)buf;
    if (n >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF)
        memmove(buf, buf + 3, n - 3 + 1);
    return buf;
}

static int parse_trace_items(const char *text, cJSON **items, int max_items) {
    int count = 0;

    /* Try as a JSON array first */
    cJSON *arr = cJSON_Parse(text);
    if (arr && cJSON_IsArray(arr)) {
        cJSON *elem;
        cJSON_ArrayForEach(elem, arr) {
            if (count >= max_items)
                break;
            if (cJSON_IsObject(elem))
                items[count++] = cJSON_Duplicate(elem, 1);
        }
        cJSON_Delete(arr);
        return count;
    }
    if (arr)
        cJSON_Delete(arr);

    /* Fall back to line-delimited JSON (JSONL) */
    const char *line = text;
    while (*line && count < max_items) {
        while (*line == '\n' || *line == '\r' || *line == ' ')
            line++;
        if (!*line)
            break;

        const char *end = strchr(line, '\n');
        if (!end)
            end = line + strlen(line);

        size_t len = (size_t)(end - line);
        char *tmp = malloc(len + 1);
        if (!tmp)
            break;
        memcpy(tmp, line, len);
        tmp[len] = '\0';

        cJSON *obj = cJSON_Parse(tmp);
        free(tmp);

        if (obj) {
            if (cJSON_IsObject(obj)) {
                items[count++] = obj;
            } else if (cJSON_IsArray(obj)) {
                cJSON *inner;
                cJSON_ArrayForEach(inner, obj) {
                    if (count >= max_items)
                        break;
                    if (cJSON_IsObject(inner))
                        items[count++] = cJSON_Duplicate(inner, 1);
                }
                cJSON_Delete(obj);
            } else {
                cJSON_Delete(obj);
            }
        }
        line = (*end) ? end + 1 : end;
    }
    return count;
}

static int normalize_items(cJSON **raw, int raw_count, cJSON **out, int max_out) {
    int count = 0;
    for (int i = 0; i < raw_count && count < max_out; i++) {
        cJSON *item = raw[i];

        /* Unwrap "response_item" payloads */
        cJSON *payload = cJSON_GetObjectItem(item, "payload");
        if (payload) {
            cJSON *evtype = cJSON_GetObjectItem(item, "type");
            if (!evtype || !cJSON_IsString(evtype) ||
                strcmp(evtype->valuestring, "response_item") != 0)
                continue;
            if (cJSON_IsObject(payload)) {
                out[count++] = cJSON_Duplicate(payload, 1);
            } else if (cJSON_IsArray(payload)) {
                cJSON *sub;
                cJSON_ArrayForEach(sub, payload) {
                    if (count >= max_out)
                        break;
                    if (cJSON_IsObject(sub))
                        out[count++] = cJSON_Duplicate(sub, 1);
                }
            }
            continue;
        }

        /* Direct items: filter by type/role */
        cJSON *item_type = cJSON_GetObjectItem(item, "type");
        if (!item_type || !cJSON_IsString(item_type))
            continue;
        if (strcmp(item_type->valuestring, "message") == 0) {
            cJSON *role = cJSON_GetObjectItem(item, "role");
            if (!role || !cJSON_IsString(role) || !is_allowed_role(role->valuestring))
                continue;
        }
        out[count++] = cJSON_Duplicate(item, 1);
    }
    return count;
}

static int build_memory_from_trace(const char *path, int index, built_memory_t *out) {
    memset(out, 0, sizeof(*out));

    char *text = load_trace_text(path);
    if (!text)
        return -1;

    cJSON *raw_items[MEM_TRACE_ITEM_MAX];
    int raw_count = parse_trace_items(text, raw_items, MEM_TRACE_ITEM_MAX);
    free(text);
    if (raw_count == 0)
        return -1;

    cJSON *normalized[MEM_TRACE_ITEM_MAX];
    int norm_count = normalize_items(raw_items, raw_count, normalized, MEM_TRACE_ITEM_MAX);
    for (int i = 0; i < raw_count; i++)
        cJSON_Delete(raw_items[i]);
    if (norm_count == 0)
        return -1;

    /* Build summary text from normalized items */
    strbuf_t sb;
    strbuf_init(&sb);
    for (int i = 0; i < norm_count; i++) {
        cJSON *item = normalized[i];
        cJSON *type_j = cJSON_GetObjectItem(item, "type");
        cJSON *role_j = cJSON_GetObjectItem(item, "role");
        cJSON *content_j = cJSON_GetObjectItem(item, "content");

        if (type_j && cJSON_IsString(type_j))
            strbuf_appendf(&sb, "[%s] ", type_j->valuestring);
        if (role_j && cJSON_IsString(role_j))
            strbuf_appendf(&sb, "(%s) ", role_j->valuestring);
        if (content_j && cJSON_IsString(content_j)) {
            size_t clen = strlen(content_j->valuestring);
            if (clen > MEM_TRACE_PREVIEW_MAX) {
                strbuf_append(&sb, content_j->valuestring, MEM_TRACE_PREVIEW_MAX);
                strbuf_append_str(&sb, "...\n");
            } else {
                strbuf_append_str(&sb, content_j->valuestring);
                strbuf_append_str(&sb, "\n");
            }
        } else {
            strbuf_append_str(&sb, "\n");
        }
    }
    for (int i = 0; i < norm_count; i++)
        cJSON_Delete(normalized[i]);

    /* Derive a memory ID from the filename stem */
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t stem_len = dot ? (size_t)(dot - base) : strlen(base);
    char stem[NC_LABEL_MAX];
    if (stem_len >= sizeof(stem))
        stem_len = sizeof(stem) - 1;
    memcpy(stem, base, stem_len);
    stem[stem_len] = '\0';

    strbuf_t id_buf;
    strbuf_init(&id_buf);
    strbuf_appendf(&id_buf, "memory_%d_%s", index, stem);

    out->memory_id = strbuf_detach(&id_buf);
    out->source_path = nc_xstrdup(path);
    out->raw_text = strbuf_detach(&sb);
    out->summary = NULL;
    return 0;
}

/* ── Public API ── */

int mem_trace_build(const char **paths, int count, built_memory_t *out, int max_out) {
    int built = 0;
    for (int i = 0; i < count && built < max_out; i++)
        if (build_memory_from_trace(paths[i], i + 1, &out[built]) == 0)
            built++;
    return built;
}

void mem_trace_free_memories(built_memory_t *mems, int count) {
    for (int i = 0; i < count; i++) {
        free(mems[i].memory_id);
        free(mems[i].source_path);
        free(mems[i].raw_text);
        free(mems[i].summary);
    }
}

char *mem_trace_format(const built_memory_t *mems, int count) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Trace memories: %d built\n", count);
    for (int i = 0; i < count; i++) {
        int raw_len = mems[i].raw_text ? (int)strlen(mems[i].raw_text) : 0;
        strbuf_appendf(&sb, "  [%s] from %s (%d chars)\n",
                       mems[i].memory_id ? mems[i].memory_id : "?",
                       mems[i].source_path ? mems[i].source_path : "?", raw_len);
    }
    return strbuf_detach(&sb);
}
