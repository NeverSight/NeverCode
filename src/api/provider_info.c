/*
 * provider_info.c — Model provider registry and per-provider configuration
 *
 * Each provider entry defines:
 * - Base URL, env key for API key
 * - Stream idle timeout, retry limits
 * - Custom HTTP headers
 * - Capabilities (vision, function calling, streaming, reasoning)
 */

#include "nevercode.h"

/* PINFO_* constants in defaults.h */

typedef struct {
    char key[PINFO_HDR_KEY_MAX];
    char value[PINFO_HDR_VAL_MAX];
} pinfo_header_t;

typedef struct {
    int stream_max_retries;
    int request_max_retries;
    int stream_idle_timeout_ms;
    int connect_timeout_ms;
    int backoff_base_ms;
    int backoff_max_ms;
} pinfo_retry_t;

typedef struct {
    bool vision;
    bool function_calling;
    bool streaming;
    bool reasoning;
    bool responses_api;
} pinfo_caps_t;

typedef struct {
    char id[PINFO_NAME_MAX];
    char name[PINFO_NAME_MAX];
    char base_url[PINFO_URL_MAX];
    char env_key[PINFO_NAME_MAX];
    pinfo_retry_t retry;
    pinfo_caps_t caps;
    pinfo_header_t headers[PINFO_MAX_HEADERS];
    int header_count;
    bool active;
} pinfo_entry_t;

typedef struct {
    pinfo_entry_t entries[PINFO_MAX_PROVIDERS];
    int count;
} pinfo_registry_t;

static pinfo_registry_t g_pinfo;

static bool url_has(const char *url, const char *needle) {
    return url && needle && strstr(url, needle) != NULL;
}

static bool is_openai_compat_url(const char *url) {
    if (!url || !*url)
        return false;
    if (url_has(url, DEFAULT_NATIVE_HOST))
        return false;
    if (url_has(url, PINFO_URL_HINT_MESSAGES))
        return false;
    return url_has(url, PINFO_URL_HINT_V1);
}

static void init_defaults(pinfo_retry_t *r) {
    r->stream_max_retries = PINFO_STREAM_MAX_RETRIES;
    r->request_max_retries = PINFO_REQUEST_MAX_RETRIES;
    r->stream_idle_timeout_ms = PINFO_STREAM_IDLE_TIMEOUT_MS;
    r->connect_timeout_ms = PINFO_WEBSOCKET_TIMEOUT_MS;
    r->backoff_base_ms = PINFO_BACKOFF_BASE_MS;
    r->backoff_max_ms = PINFO_BACKOFF_MAX_MS;
}

void pinfo_init(void) {
    memset(&g_pinfo, 0, sizeof(g_pinfo));

    /* Built-in: Compat API */
    pinfo_entry_t *oai = &g_pinfo.entries[0];
    nc_strlcpy(oai->id, PINFO_ID_COMPAT, sizeof(oai->id));
    nc_strlcpy(oai->name, PINFO_NAME_COMPAT, sizeof(oai->name));
    nc_strlcpy(oai->base_url, DEFAULT_COMPAT_URL, sizeof(oai->base_url));
    nc_strlcpy(oai->env_key, ENV_COMPAT_KEY, sizeof(oai->env_key));
    init_defaults(&oai->retry);
    oai->caps = (pinfo_caps_t){true, true, true, true, true};
    oai->active = true;

    /* Built-in: Native API */
    pinfo_entry_t *ant = &g_pinfo.entries[1];
    nc_strlcpy(ant->id, PINFO_ID_NATIVE, sizeof(ant->id));
    nc_strlcpy(ant->name, PINFO_NAME_NATIVE, sizeof(ant->name));
    nc_strlcpy(ant->base_url, DEFAULT_NATIVE_URL, sizeof(ant->base_url));
    nc_strlcpy(ant->env_key, ENV_NATIVE_KEY, sizeof(ant->env_key));
    init_defaults(&ant->retry);
    ant->caps = (pinfo_caps_t){true, true, true, true, false};
    ant->active = true;

    g_pinfo.count = 2;
}

static int find_provider(const char *id) {
    for (int i = 0; i < g_pinfo.count; i++)
        if (g_pinfo.entries[i].active && strcmp(g_pinfo.entries[i].id, id) == 0)
            return i;
    return -1;
}

int pinfo_register(const char *id, const char *name, const char *base_url, const char *env_key) {
    if (!id || g_pinfo.count >= PINFO_MAX_PROVIDERS)
        return -1;
    if (find_provider(id) >= 0)
        return -1;

    pinfo_entry_t *e = &g_pinfo.entries[g_pinfo.count];
    memset(e, 0, sizeof(*e));
    nc_strlcpy(e->id, id, sizeof(e->id));
    nc_strlcpy(e->name, name ? name : id, sizeof(e->name));
    if (base_url)
        nc_strlcpy(e->base_url, base_url, sizeof(e->base_url));
    if (env_key)
        nc_strlcpy(e->env_key, env_key, sizeof(e->env_key));
    init_defaults(&e->retry);
    e->active = true;
    g_pinfo.count++;
    return 0;
}

int pinfo_add_header(const char *provider_id, const char *key, const char *value) {
    int idx = find_provider(provider_id);
    if (idx < 0)
        return -1;
    pinfo_entry_t *e = &g_pinfo.entries[idx];
    if (e->header_count >= PINFO_MAX_HEADERS)
        return -1;
    pinfo_header_t *h = &e->headers[e->header_count];
    nc_strlcpy(h->key, key, sizeof(h->key));
    nc_strlcpy(h->value, value, sizeof(h->value));
    e->header_count++;
    return 0;
}

int pinfo_set_retry(const char *provider_id, int stream_retries, int request_retries,
                    int idle_timeout_ms) {
    int idx = find_provider(provider_id);
    if (idx < 0)
        return -1;
    pinfo_entry_t *e = &g_pinfo.entries[idx];
    if (stream_retries > 0)
        e->retry.stream_max_retries =
            stream_retries > PINFO_RETRY_CAP ? PINFO_RETRY_CAP : stream_retries;
    if (request_retries > 0)
        e->retry.request_max_retries =
            request_retries > PINFO_RETRY_CAP ? PINFO_RETRY_CAP : request_retries;
    if (idle_timeout_ms > 0)
        e->retry.stream_idle_timeout_ms = idle_timeout_ms;
    return 0;
}

int pinfo_set_caps(const char *provider_id, bool vision, bool func_call, bool streaming,
                   bool reasoning) {
    int idx = find_provider(provider_id);
    if (idx < 0)
        return -1;
    pinfo_entry_t *e = &g_pinfo.entries[idx];
    e->caps.vision = vision;
    e->caps.function_calling = func_call;
    e->caps.streaming = streaming;
    e->caps.reasoning = reasoning;
    return 0;
}

const char *pinfo_base_url(const char *provider_id) {
    int idx = find_provider(provider_id);
    return idx >= 0 ? g_pinfo.entries[idx].base_url : NULL;
}

const char *pinfo_env_key(const char *provider_id) {
    int idx = find_provider(provider_id);
    return idx >= 0 ? g_pinfo.entries[idx].env_key : NULL;
}

bool pinfo_has_vision(const char *provider_id) {
    int idx = find_provider(provider_id);
    return idx >= 0 && g_pinfo.entries[idx].caps.vision;
}

bool pinfo_has_reasoning(const char *provider_id) {
    int idx = find_provider(provider_id);
    return idx >= 0 && g_pinfo.entries[idx].caps.reasoning;
}

int pinfo_stream_retries(const char *provider_id) {
    int idx = find_provider(provider_id);
    return idx >= 0 ? g_pinfo.entries[idx].retry.stream_max_retries : PINFO_STREAM_MAX_RETRIES;
}

int pinfo_idle_timeout_ms(const char *provider_id) {
    int idx = find_provider(provider_id);
    return idx >= 0 ? g_pinfo.entries[idx].retry.stream_idle_timeout_ms
                    : PINFO_STREAM_IDLE_TIMEOUT_MS;
}

const char *pinfo_detect_provider_id(const char *url) {
    if (!url || !*url)
        return NULL;
    if (url_has(url, "openrouter.ai"))
        return PINFO_ID_OPENROUTER;
    if (url_has(url, "nousresearch.com"))
        return PINFO_ID_NOUS;
    if (url_has(url, "ai-gateway.vercel"))
        return PINFO_ID_AI_GATEWAY;
    if (url_has(url, DEFAULT_NATIVE_HOST) || url_has(url, PINFO_URL_HINT_MESSAGES))
        return PINFO_ID_NATIVE;
    if (url_has(url, DEFAULT_COMPAT_HOST) || is_openai_compat_url(url))
        return PINFO_ID_COMPAT;
    return NULL;
}

int pinfo_auto_register_from_url(const char *url) {
    if (!url || !*url)
        return -1;
    const char *id = pinfo_detect_provider_id(url);
    if (!id)
        return -1;
    if (find_provider(id) >= 0)
        return 0;

    if (strcmp(id, PINFO_ID_OPENROUTER) == 0)
        return pinfo_register(PINFO_ID_OPENROUTER, PINFO_NAME_OPENROUTER, PROVIDER_OPENROUTER_URL,
                              ENV_OPENROUTER_KEY);
    if (strcmp(id, PINFO_ID_NOUS) == 0)
        return pinfo_register(PINFO_ID_NOUS, PINFO_NAME_NOUS, PROVIDER_NOUS_URL, ENV_NOUS_KEY);
    if (strcmp(id, PINFO_ID_AI_GATEWAY) == 0)
        return pinfo_register(PINFO_ID_AI_GATEWAY, PINFO_NAME_AI_GATEWAY, PROVIDER_AI_GATEWAY_URL,
                              ENV_COMPAT_KEY);
    return -1;
}

int pinfo_count(void) {
    return g_pinfo.count;
}

char *pinfo_format_list(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Providers (%d):\n", g_pinfo.count);
    for (int i = 0; i < g_pinfo.count; i++) {
        pinfo_entry_t *e = &g_pinfo.entries[i];
        if (!e->active)
            continue;
        strbuf_appendf(&sb, "  [%s] %s — %s (retries: stream=%d req=%d, idle=%dms)\n", e->id,
                       e->name, e->base_url[0] ? e->base_url : "(none)",
                       e->retry.stream_max_retries, e->retry.request_max_retries,
                       e->retry.stream_idle_timeout_ms);
        if (e->header_count > 0)
            strbuf_appendf(&sb, "    custom headers: %d\n", e->header_count);
    }
    return strbuf_detach(&sb);
}
