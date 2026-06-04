/*
 * plugin_toggle.c — Plugin enable/disable toggles
 *
 * Manages per-session plugin activation state.
 * Plugins can be toggled without full uninstall.
 */

#include "nevercode.h"
#include "defaults.h"

/* ── Types ── */

typedef struct {
    char id[TOOL_POOL_NAME_MAX];
    bool enabled;
    long toggled_at;
} plugin_toggle_entry_t;

typedef struct {
    plugin_toggle_entry_t entries[MAX_PLUGINS];
    int count;
    bool initialized;
} plugin_toggle_ctx_t;

static plugin_toggle_ctx_t g_toggles;

/* ── Init / Free ── */

void plugin_toggle_init(void) {
    memset(&g_toggles, 0, sizeof(g_toggles));
    g_toggles.initialized = true;
}

void plugin_toggle_free(void) {
    g_toggles.count = 0;
    g_toggles.initialized = false;
}

/* ── Lookup / Set ── */

static int find_toggle(const char *id) {
    for (int i = 0; i < g_toggles.count; i++)
        if (strcmp(g_toggles.entries[i].id, id) == 0)
            return i;
    return -1;
}

bool plugin_toggle_is_enabled(const char *id) {
    if (!id)
        return false;
    int idx = find_toggle(id);
    if (idx < 0)
        return true; /* unknown plugins default to enabled */
    return g_toggles.entries[idx].enabled;
}

int plugin_toggle_set(const char *id, bool enabled) {
    if (!id || !g_toggles.initialized)
        return -1;

    int idx = find_toggle(id);
    if (idx >= 0) {
        g_toggles.entries[idx].enabled = enabled;
        g_toggles.entries[idx].toggled_at = nc_wall_sec();
        return 0;
    }

    if (g_toggles.count >= MAX_PLUGINS)
        return -1;

    plugin_toggle_entry_t *e = &g_toggles.entries[g_toggles.count++];
    nc_strlcpy(e->id, id, sizeof(e->id));
    e->enabled = enabled;
    e->toggled_at = nc_wall_sec();
    return 0;
}

int plugin_toggle_enable(const char *id) {
    return plugin_toggle_set(id, true);
}

int plugin_toggle_disable(const char *id) {
    return plugin_toggle_set(id, false);
}

/* ── Queries ── */

int plugin_toggle_count(void) {
    return g_toggles.count;
}

int plugin_toggle_enabled_count(void) {
    int c = 0;
    for (int i = 0; i < g_toggles.count; i++)
        if (g_toggles.entries[i].enabled)
            c++;
    return c;
}

int plugin_toggle_disabled_count(void) {
    return g_toggles.count - plugin_toggle_enabled_count();
}

/* ── Serialization ── */

char *plugin_toggle_serialize(void) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < g_toggles.count; i++) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "id", g_toggles.entries[i].id);
        cJSON_AddBoolToObject(obj, "enabled", g_toggles.entries[i].enabled);
        cJSON_AddNumberToObject(obj, "toggled_at", g_toggles.entries[i].toggled_at);
        cJSON_AddItemToArray(arr, obj);
    }
    char *out = nc_cjson_detach(arr);
    return out;
}

int plugin_toggle_deserialize(const char *json) {
    if (!json)
        return -1;
    cJSON *arr = cJSON_Parse(json);
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        return -1;
    }

    g_toggles.count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        if (g_toggles.count >= MAX_PLUGINS)
            break;
        cJSON *jid = cJSON_GetObjectItem(item, "id");
        cJSON *jen = cJSON_GetObjectItem(item, "enabled");
        cJSON *jat = cJSON_GetObjectItem(item, "toggled_at");
        if (!jid || !cJSON_IsString(jid))
            continue;

        plugin_toggle_entry_t *e = &g_toggles.entries[g_toggles.count++];
        nc_strlcpy(e->id, jid->valuestring, sizeof(e->id));
        e->enabled = jen ? cJSON_IsTrue(jen) : true;
        e->toggled_at = jat ? (long)jat->valuedouble : 0;
    }

    cJSON_Delete(arr);
    return g_toggles.count;
}

char *plugin_toggle_format(void) {
    if (g_toggles.count == 0)
        return nc_xstrdup("No plugin toggles.\n");

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Plugin toggles (%d):\n", g_toggles.count);
    for (int i = 0; i < g_toggles.count; i++) {
        const plugin_toggle_entry_t *e = &g_toggles.entries[i];
        strbuf_appendf(&sb, "  %s: %s\n", e->id, e->enabled ? "enabled" : "disabled");
    }
    return strbuf_detach(&sb);
}
