#include "nevercode.h"

void memory_init(memory_ctx_t *m) {
    memset(m, 0, sizeof(*m));
}

void memory_free(memory_ctx_t *m) {
    for (int i = 0; i < m->len; i++) {
        free(m->entries[i].key);
        free(m->entries[i].value);
    }
    free(m->entries);
    memset(m, 0, sizeof(*m));
}

static void memory_grow(memory_ctx_t *m) {
    if (m->len >= m->cap) {
        int newcap = m->cap ? m->cap * 2 : 16;
        memory_entry_t *tmp = realloc(m->entries, newcap * sizeof(memory_entry_t));
        if (!tmp)
            return;
        m->entries = tmp;
        m->cap = newcap;
    }
}

void memory_set(memory_ctx_t *m, const char *key, const char *value) {
    if (!key || !value)
        return;

    for (int i = 0; i < m->len; i++) {
        if (strcmp(m->entries[i].key, key) == 0) {
            free(m->entries[i].value);
            m->entries[i].value = nc_xstrdup(value);
            m->entries[i].timestamp = nc_wall_sec();
            return;
        }
    }

    memory_grow(m);
    m->entries[m->len].key = nc_xstrdup(key);
    m->entries[m->len].value = nc_xstrdup(value);
    m->entries[m->len].timestamp = nc_wall_sec();
    m->len++;
}

const char *memory_get(const memory_ctx_t *m, const char *key) {
    if (!key)
        return NULL;
    for (int i = 0; i < m->len; i++) {
        if (strcmp(m->entries[i].key, key) == 0)
            return m->entries[i].value;
    }
    return NULL;
}

int memory_load(memory_ctx_t *m, const char *path) {
    cJSON *root = nc_json_load(path);
    if (!root)
        return -1;

    cJSON *arr = cJSON_GetObjectItem(root, "memories");
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return 0;
    }

    int count = cJSON_GetArraySize(arr);
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        const char *key = cJSON_GetStringValue(cJSON_GetObjectItem(item, "key"));
        const char *val = cJSON_GetStringValue(cJSON_GetObjectItem(item, "value"));
        cJSON *ts = cJSON_GetObjectItem(item, "timestamp");
        if (!key || !val)
            continue;

        memory_grow(m);
        m->entries[m->len].key = nc_xstrdup(key);
        m->entries[m->len].value = nc_xstrdup(val);
        m->entries[m->len].timestamp = ts && cJSON_IsNumber(ts) ? (long)ts->valuedouble : 0;
        m->len++;
    }

    cJSON_Delete(root);
    return m->len;
}

int memory_save(const memory_ctx_t *m, const char *path) {
    char dir[NC_PATH_MAX];
    nc_strlcpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, NC_DIR_MODE);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "memories");

    for (int i = 0; i < m->len; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "key", m->entries[i].key);
        cJSON_AddStringToObject(item, "value", m->entries[i].value);
        cJSON_AddNumberToObject(item, "timestamp", m->entries[i].timestamp);
        cJSON_AddItemToArray(arr, item);
    }

    char *json = nc_cjson_detach_pretty(root);

    int rc = nc_write_file_str(path, json);
    free(json);
    return rc;
}

int memory_delete(memory_ctx_t *m, const char *key) {
    if (!key)
        return -1;
    for (int i = 0; i < m->len; i++) {
        if (strcmp(m->entries[i].key, key) == 0) {
            free(m->entries[i].key);
            free(m->entries[i].value);
            for (int j = i; j < m->len - 1; j++)
                m->entries[j] = m->entries[j + 1];
            m->len--;
            return 0;
        }
    }
    return -1;
}

int memory_count(const memory_ctx_t *m) {
    return m ? m->len : 0;
}

int memory_consolidate(memory_ctx_t *m) {
    if (!m || m->len < 6)
        return 0;

    int auto_count = 0;
    for (int i = 0; i < m->len; i++)
        if (nc_starts_with(m->entries[i].key, "auto_"))
            auto_count++;
    if (auto_count < 4)
        return 0;

    /* Group auto-memories by category prefix (e.g. auto_project, auto_stack) */
    typedef struct {
        const char *cat;
        strbuf_t merged;
        int count;
    } group_t;
    group_t groups[16];
    int ngroups = 0;

    for (int i = 0; i < m->len; i++) {
        if (!nc_starts_with(m->entries[i].key, "auto_"))
            continue;

        const char *key = m->entries[i].key + 5;
        char cat[NC_SHORT_MAX] = {0};
        int ci = 0;
        while (key[ci] && key[ci] != '_' && ci < 30) {
            cat[ci] = key[ci];
            ci++;
        }
        cat[ci] = '\0';

        int gi = -1;
        for (int g = 0; g < ngroups; g++) {
            if (strcmp(groups[g].cat, cat) == 0) {
                gi = g;
                break;
            }
        }
        if (gi < 0 && ngroups < 16) {
            gi = ngroups++;
            groups[gi].cat = nc_xstrdup(cat);
            strbuf_init(&groups[gi].merged);
            groups[gi].count = 0;
        }
        if (gi >= 0) {
            if (groups[gi].count > 0)
                strbuf_append_str(&groups[gi].merged, "; ");
            strbuf_append_str(&groups[gi].merged, m->entries[i].value);
            groups[gi].count++;
        }
    }

    /* Replace multi-entry groups with single consolidated entry */
    int consolidated = 0;
    for (int g = 0; g < ngroups; g++) {
        if (groups[g].count <= 1) {
            strbuf_free(&groups[g].merged);
            free((void *)groups[g].cat);
            continue;
        }

        char prefix[NC_FMT_MAX];
        snprintf(prefix, sizeof(prefix), "auto_%s_", groups[g].cat);
        for (int i = m->len - 1; i >= 0; i--) {
            if (nc_starts_with(m->entries[i].key, prefix))
                memory_delete(m, m->entries[i].key);
        }

        char new_key[NC_FMT_MAX];
        snprintf(new_key, sizeof(new_key), "auto_%s", groups[g].cat);
        memory_set(m, new_key, groups[g].merged.data);
        consolidated += groups[g].count - 1;

        strbuf_free(&groups[g].merged);
        free((void *)groups[g].cat);
    }

    return consolidated;
}

char *memory_format_for_prompt(const memory_ctx_t *m) {
    if (m->len == 0)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "# User Memories\n\n");
    strbuf_append_str(&sb, "The following are key facts and preferences "
                           "remembered from previous conversations:\n\n");

    for (int i = 0; i < m->len; i++) {
        strbuf_append_str(&sb, "- **");
        strbuf_append_str(&sb, m->entries[i].key);
        strbuf_append_str(&sb, "**: ");
        strbuf_append_str(&sb, m->entries[i].value);
        strbuf_append_str(&sb, "\n");
    }

    return strbuf_detach(&sb);
}
