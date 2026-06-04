/*
 * plugin_store.c — Plugin state persistence
 *
 * Tracks installed plugin state (enabled/disabled, versions, config)
 * across sessions using a JSON state file.
 */

#include "nevercode.h"
#include "defaults.h"

#define PSTORE_MAX_ENTRIES MAX_PLUGINS
/* PSTORE_STATE_FILENAME in defaults.h */
#define PSTORE_FILENAME PSTORE_STATE_FILENAME

typedef struct {
    char name[NC_FMT_MAX];
    char version[NC_SHORT_MAX];
    bool enabled;
    bool pinned;
    long install_time;
} pstore_entry_t;

typedef struct {
    pstore_entry_t entries[PSTORE_MAX_ENTRIES];
    int count;
    char path[NC_PATH_MAX];
    bool dirty;
} pstore_t;

static pstore_t g_store;

static int find_entry(const char *name) {
    for (int i = 0; i < g_store.count; i++)
        if (strcmp(g_store.entries[i].name, name) == 0)
            return i;
    return -1;
}

int pstore_init(const char *home) {
    memset(&g_store, 0, sizeof(g_store));
    if (!home)
        return -1;
    snprintf(g_store.path, sizeof(g_store.path), "%s%s/%s", home, CONFIG_DIR_PRIMARY,
             PSTORE_FILENAME);
    return 0;
}

int pstore_load(void) {
    char *buf = strbuf_read_file_max(g_store.path, NULL, NC_READ_CAP_JSON_STATE);
    if (!buf) {
        struct stat st;
        if (stat(g_store.path, &st) != 0)
            return 0;
        return -1;
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
        return -1;

    const cJSON *arr = cJSON_GetObjectItem(root, "plugins");
    if (arr && cJSON_IsArray(arr)) {
        const cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            if (g_store.count >= PSTORE_MAX_ENTRIES)
                break;

            pstore_entry_t *e = &g_store.entries[g_store.count];
            memset(e, 0, sizeof(*e));

            nc_strlcpy(e->name, nc_cjson_str(it, "name", ""), sizeof(e->name));
            nc_strlcpy(e->version, nc_cjson_str(it, "version", ""), sizeof(e->version));
            e->enabled = nc_cjson_bool(it, "enabled", true);
            e->pinned = nc_cjson_bool(it, "pinned", false);
            e->install_time = (long)nc_cjson_dbl(it, "install_time", 0);

            g_store.count++;
        }
    }

    cJSON_Delete(root);
    return g_store.count;
}

int pstore_save(void) {
    if (!g_store.dirty)
        return 0;

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "plugins");

    for (int i = 0; i < g_store.count; i++) {
        pstore_entry_t *e = &g_store.entries[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "name", e->name);
        cJSON_AddStringToObject(obj, "version", e->version);
        cJSON_AddBoolToObject(obj, "enabled", e->enabled);
        cJSON_AddBoolToObject(obj, "pinned", e->pinned);
        cJSON_AddNumberToObject(obj, "install_time", (double)e->install_time);
        cJSON_AddItemToArray(arr, obj);
    }

    int rc = nc_json_save(g_store.path, root);
    cJSON_Delete(root);
    if (rc == 0)
        g_store.dirty = false;
    return rc;
}

int pstore_set_enabled(const char *name, bool enabled) {
    int idx = find_entry(name);
    if (idx < 0)
        return -1;
    g_store.entries[idx].enabled = enabled;
    g_store.dirty = true;
    return 0;
}

int pstore_add(const char *name, const char *version) {
    if (!name || !name[0] || g_store.count >= PSTORE_MAX_ENTRIES)
        return -1;
    if (find_entry(name) >= 0)
        return -1;

    pstore_entry_t *e = &g_store.entries[g_store.count];
    memset(e, 0, sizeof(*e));
    nc_strlcpy(e->name, name, sizeof(e->name));
    if (version)
        nc_strlcpy(e->version, version, sizeof(e->version));
    e->enabled = true;
    e->install_time = nc_wall_sec();
    g_store.count++;
    g_store.dirty = true;
    return 0;
}

int pstore_remove(const char *name) {
    int idx = find_entry(name);
    if (idx < 0)
        return -1;
    if (idx < g_store.count - 1)
        memmove(&g_store.entries[idx], &g_store.entries[idx + 1],
                (size_t)(g_store.count - idx - 1) * sizeof(pstore_entry_t));
    g_store.count--;
    g_store.dirty = true;
    return 0;
}

bool pstore_is_enabled(const char *name) {
    int idx = find_entry(name);
    return idx >= 0 && g_store.entries[idx].enabled;
}

int pstore_count(void) {
    return g_store.count;
}

int pstore_enabled_count(void) {
    int n = 0;
    for (int i = 0; i < g_store.count; i++)
        if (g_store.entries[i].enabled)
            n++;
    return n;
}
