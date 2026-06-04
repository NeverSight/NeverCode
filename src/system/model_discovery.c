/*
 * model_discovery.c — Dynamic model list from API /models endpoint
 */
#include "nevercode.h"
#include "model_registry.h"
#include <curl/curl.h>

/* MODEL_DISCOVERY_CACHE_FILENAME in defaults.h */
#define MODEL_CACHE_FILE MODEL_DISCOVERY_CACHE_FILENAME

typedef struct {
    char *id;
    char *display_name;
    int context_window;
    bool supports_tools;
    bool supports_vision;
    long fetched_at;
} discovered_model_t;
static discovered_model_t g_discovered[MAX_DISCOVERED_MODELS];
static int g_discovered_count = 0;
static long g_last_fetch = 0;
static bool g_fetch_in_progress = false;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} fetch_buf_t;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *ud) {
    fetch_buf_t *buf = ud;
    size_t total = size * nmemb;
    if (buf->len + total >= buf->cap) {
        size_t nc = (buf->len + total) * 2 + 1;
        char *tmp = realloc(buf->data, nc);
        if (!tmp)
            return 0;
        buf->data = tmp;
        buf->cap = nc;
    }
    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

static void clear_discovered(void) {
    for (int i = 0; i < g_discovered_count; i++) {
        free(g_discovered[i].id);
        free(g_discovered[i].display_name);
    }
    g_discovered_count = 0;
}

static int parse_models_response(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return -1;
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data || !cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return -1;
    }
    clear_discovered();
    long now = nc_wall_sec();
    cJSON *item;
    cJSON_ArrayForEach(item, data) {
        if (g_discovered_count >= MAX_DISCOVERED_MODELS)
            break;
        const char *id_str = nc_cjson_str(item, "id", NULL);
        if (!id_str)
            continue;
        discovered_model_t *m = &g_discovered[g_discovered_count];
        m->id = nc_xstrdup(id_str);
        m->display_name = nc_cjson_dup_str(item, "name", id_str);
        m->context_window = nc_cjson_int(item, "context_window", 0);
        m->supports_tools = true;
        m->supports_vision = false;
        m->fetched_at = now;
        g_discovered_count++;
    }
    cJSON_Delete(root);
    return g_discovered_count;
}

static int save_cache(void) {
    char path[NC_PATH_MAX];
    config_home_path(path, sizeof(path), MODEL_CACHE_FILE);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "fetched_at", (double)g_last_fetch);
    cJSON *arr = cJSON_AddArrayToObject(root, "models");
    for (int i = 0; i < g_discovered_count; i++) {
        cJSON *m = cJSON_CreateObject();
        cJSON_AddStringToObject(m, "id", g_discovered[i].id);
        if (g_discovered[i].display_name)
            cJSON_AddStringToObject(m, "display_name", g_discovered[i].display_name);
        if (g_discovered[i].context_window > 0)
            cJSON_AddNumberToObject(m, "context_window", g_discovered[i].context_window);
        cJSON_AddItemToArray(arr, m);
    }
    char *json = nc_cjson_detach_pretty(root);
    if (!json)
        return -1;
    int rc = nc_write_file_str(path, json);
    free(json);
    return rc;
}

int model_discovery_fetch(const char *base_url, const char *api_key) {
    if (!base_url || !api_key)
        return -1;
    if (g_fetch_in_progress)
        return 0;
    g_fetch_in_progress = true;
    char url[NC_URL_MAX];
    snprintf(url, sizeof(url), "%s/models", base_url);
    CURL *curl = curl_easy_init();
    if (!curl) {
        g_fetch_in_progress = false;
        return -1;
    }
    fetch_buf_t buf = {0};
    buf.cap = FETCH_BUF_INITIAL;
    buf.data = calloc(1, buf.cap);
    if (!buf.data) {
        curl_easy_cleanup(curl);
        return -1;
    }
    struct curl_slist *hdrs = NULL;
    char auth[NC_MSG_MAX];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", api_key);
    hdrs = curl_slist_append(hdrs, auth);
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)MODEL_DISCOVERY_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    int result = -1;
    if (res == CURLE_OK && buf.data) {
        result = parse_models_response(buf.data);
        if (result > 0) {
            g_last_fetch = nc_wall_sec();
            save_cache();
        }
    }
    free(buf.data);
    g_fetch_in_progress = false;
    return result;
}

bool model_discovery_cache_valid(void) {
    return g_last_fetch > 0 && (nc_wall_sec() - g_last_fetch) < MODEL_DISCOVERY_CACHE_TTL_S;
}
int model_discovery_count(void) {
    return g_discovered_count;
}
const char *model_discovery_id(int idx) {
    return (idx >= 0 && idx < g_discovered_count) ? g_discovered[idx].id : NULL;
}
const char *model_discovery_name(int idx) {
    return (idx >= 0 && idx < g_discovered_count) ? g_discovered[idx].display_name : NULL;
}
bool model_discovery_exists(const char *model_id) {
    if (!model_id)
        return false;
    for (int i = 0; i < g_discovered_count; i++)
        if (g_discovered[i].id && strcmp(g_discovered[i].id, model_id) == 0)
            return true;
    return model_registry_find(model_id) != NULL;
}
static const int CTX_PROBE_TIERS[] = {
    MODEL_CTX_PROBE_TIER_0, MODEL_CTX_PROBE_TIER_1, MODEL_CTX_PROBE_TIER_2,
    MODEL_CTX_PROBE_TIER_3, MODEL_CTX_PROBE_TIER_4,
};

int model_discovery_context_window(const char *model_id) {
    if (!model_id)
        return 0;
    for (int i = 0; i < g_discovered_count; i++)
        if (g_discovered[i].id && strcmp(g_discovered[i].id, model_id) == 0)
            return g_discovered[i].context_window;
    const model_info_t *info = model_registry_find(model_id);
    if (info)
        return info->context_length;
    /* For unrecognized models, return the first (most optimistic) probe tier
     * as the conservative default — the harness can step down if the API
     * returns a context-length error. */
    return CTX_PROBE_TIERS[0];
}

int model_discovery_probe_tier(int tier_idx) {
    if (tier_idx < 0 || tier_idx >= MODEL_CTX_PROBE_TIERS)
        return 0;
    return CTX_PROBE_TIERS[tier_idx];
}

int model_discovery_probe_tier_count(void) {
    return MODEL_CTX_PROBE_TIERS;
}
void model_discovery_clear(void) {
    clear_discovered();
    g_last_fetch = 0;
}
char *model_discovery_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Discovered Models (%d):\n", g_discovered_count);
    for (int i = 0; i < g_discovered_count; i++) {
        strbuf_appendf(&sb, "  %-40s", g_discovered[i].id);
        if (g_discovered[i].context_window > 0)
            strbuf_appendf(&sb, "  ctx=%dk", g_discovered[i].context_window / 1024);
        strbuf_append_str(&sb, "\n");
    }
    if (g_last_fetch > 0) {
        long age = nc_wall_sec() - g_last_fetch;
        strbuf_appendf(&sb, "Cache age: %lds (TTL: %ds)\n", age, MODEL_DISCOVERY_CACHE_TTL_S);
    }
    return strbuf_detach(&sb);
}
