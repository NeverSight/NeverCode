/*
 * models_mgr.c — Remote model discovery and cache management.
 *
 * Fetches available models from the provider's /models endpoint,
 * caches results to disk with TTL, and merges with the local
 * model registry for unified lookup.
 */
#include "nevercode.h"
#include "defaults.h"
#include <curl/curl.h>

typedef struct {
    char id[MODEL_NAME_MAX];
    char display_name[MODEL_NAME_MAX];
    int context_window;
    int max_output;
    bool supports_tools;
    bool supports_vision;
    long fetched_at;
} remote_model_t;

static struct {
    remote_model_t models[MODELS_MGR_MAX_REMOTE];
    int count;
    long last_refresh;
    int ttl_s;
    char cache_path[NC_PATH_MAX];
    bool initialized;
} g_mm;

void models_mgr_init(void) {
    memset(&g_mm, 0, sizeof(g_mm));
    g_mm.ttl_s = MODELS_MGR_CACHE_TTL_S;
    config_home_path(g_mm.cache_path, sizeof(g_mm.cache_path), MODEL_DISCOVERY_CACHE_FILENAME);
    g_mm.initialized = true;
}

void models_mgr_free(void) {
    memset(&g_mm, 0, sizeof(g_mm));
}

int models_mgr_count(void) {
    return g_mm.count;
}

bool models_mgr_stale(void) {
    return nc_wall_sec() - g_mm.last_refresh > g_mm.ttl_s;
}

int models_mgr_find(const char *id) {
    if (!id)
        return -1;
    for (int i = 0; i < g_mm.count; i++)
        if (strcmp(g_mm.models[i].id, id) == 0)
            return i;
    return -1;
}

int models_mgr_context_window(int idx) {
    if (idx < 0 || idx >= g_mm.count)
        return 0;
    return g_mm.models[idx].context_window;
}

char *models_mgr_list_json(void) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < g_mm.count; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", g_mm.models[i].id);
        cJSON_AddNumberToObject(o, "context_window", g_mm.models[i].context_window);
        cJSON_AddNumberToObject(o, "max_output", g_mm.models[i].max_output);
        cJSON_AddItemToArray(arr, o);
    }
    return nc_cjson_detach(arr);
}
