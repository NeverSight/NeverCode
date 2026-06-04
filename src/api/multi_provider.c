/*
 * multi_provider.c — Multi-provider fallback for API calls.
 * When the primary provider fails with a non-retryable or exhausted error,
 * automatically switch to a fallback provider and retry.
 *
 * Configuration: config.json
 *   - { "fallback": { "base_url": "...", "api_key": "...", "model": "..." } }
 *   - { "fallback_providers": [ ... ] }
 */

#include "nevercode.h"
#include "config.h"
#include "api.h"
#include "api_compat.h"
#include "terminal.h"

/* MAX_FALLBACK_PROVIDERS in defaults.h */

typedef struct {
    char *base_url;
    char *api_key;
    char *model;
    provider_type_t type;
    bool active;
} fallback_entry_t;

static struct {
    fallback_entry_t providers[MAX_FALLBACK_PROVIDERS];
    int count;
    bool initialized;
} g_fallback = {0};

void multi_provider_init(void) {
    memset(&g_fallback, 0, sizeof(g_fallback));
    g_fallback.initialized = true;
}

void multi_provider_free(void) {
    for (int i = 0; i < g_fallback.count; i++) {
        free(g_fallback.providers[i].base_url);
        free(g_fallback.providers[i].api_key);
        free(g_fallback.providers[i].model);
        g_fallback.providers[i].base_url = NULL;
        g_fallback.providers[i].api_key = NULL;
        g_fallback.providers[i].model = NULL;
    }
    g_fallback.count = 0;
    g_fallback.initialized = false;
}

int multi_provider_add(const char *base_url, const char *api_key, const char *model,
                       provider_type_t type) {
    if (!base_url || !api_key)
        return -1;
    if (g_fallback.count >= MAX_FALLBACK_PROVIDERS)
        return -1;

    fallback_entry_t *e = &g_fallback.providers[g_fallback.count];
    e->base_url = nc_xstrdup(base_url);
    e->api_key = nc_xstrdup(api_key);
    e->model = model ? nc_xstrdup(model) : NULL;
    e->type = type;
    e->active = true;
    g_fallback.count++;
    return 0;
}

int multi_provider_count(void) {
    return g_fallback.count;
}

bool multi_provider_has_fallback(void) {
    return g_fallback.count > 0;
}

const char *multi_provider_url(int idx) {
    if (idx < 0 || idx >= g_fallback.count)
        return NULL;
    return g_fallback.providers[idx].base_url;
}

const char *multi_provider_key(int idx) {
    if (idx < 0 || idx >= g_fallback.count)
        return NULL;
    return g_fallback.providers[idx].api_key;
}

const char *multi_provider_model(int idx) {
    if (idx < 0 || idx >= g_fallback.count)
        return NULL;
    return g_fallback.providers[idx].model;
}

provider_type_t multi_provider_type(int idx) {
    if (idx < 0 || idx >= g_fallback.count)
        return PROVIDER_COMPAT;
    return g_fallback.providers[idx].type;
}

bool multi_provider_active(int idx) {
    if (idx < 0 || idx >= g_fallback.count)
        return false;
    return g_fallback.providers[idx].active;
}

void multi_provider_disable(int idx) {
    if (idx >= 0 && idx < g_fallback.count)
        g_fallback.providers[idx].active = false;
}

void multi_provider_enable(int idx) {
    if (idx >= 0 && idx < g_fallback.count)
        g_fallback.providers[idx].active = true;
}

void multi_provider_enable_all(void) {
    for (int i = 0; i < g_fallback.count; i++)
        g_fallback.providers[i].active = true;
}

static bool is_fallback_worthy_error(const char *error_msg) {
    if (!error_msg)
        return false;
    if (strstr(error_msg, HTTP_STR_401) || strstr(error_msg, HTTP_ERR_UNAUTHORIZED))
        return true;
    if (strstr(error_msg, HTTP_STR_403) || strstr(error_msg, HTTP_ERR_FORBIDDEN))
        return true;
    if (strstr(error_msg, NET_ERR_CONNECTION) && strstr(error_msg, "refused"))
        return true;
    if (strstr(error_msg, NET_ERR_CONNECT))
        return true;
    if (strstr(error_msg, NET_ERR_RESOLVE))
        return true;
    if (strstr(error_msg, HTTP_STR_503) || strstr(error_msg, HTTP_ERR_UNAVAILABLE))
        return true;
    if (strstr(error_msg, HTTP_STR_529) || strstr(error_msg, HTTP_ERR_OVERLOADED))
        return true;
    return false;
}

typedef struct {
    stream_callback_t real_cb;
    void *real_ud;
    bool had_error;
    bool had_success;
    char error_msg[NC_MSG_MAX];
} fallback_wrapper_t;

static void fallback_intercept_cb(const stream_event_t *ev, void *ud) {
    fallback_wrapper_t *fw = ud;

    if (ev->type == STREAM_EVENT_TEXT_DELTA || ev->type == STREAM_EVENT_TOOL_USE_BEGIN) {
        fw->had_success = true;
    }

    if (ev->type == STREAM_EVENT_ERROR && ev->error_message) {
        fw->had_error = true;
        nc_strlcpy(fw->error_msg, ev->error_message, sizeof(fw->error_msg));
        if (fw->had_success) {
            fw->real_cb(ev, fw->real_ud);
        }
        return;
    }

    fw->real_cb(ev, fw->real_ud);
}

int multi_provider_stream_with_fallback(const message_t *msgs, int n, const char *sys,
                                        const config_t *primary_cfg, const tool_def_t *tools,
                                        int nt, stream_callback_t cb, void *ud) {
    int rc = api_stream_request(msgs, n, sys, primary_cfg, tools, nt, cb, ud);
    if (rc == 0 || g_interrupted || g_fallback.count == 0)
        return rc;

    for (int i = 0; i < g_fallback.count; i++) {
        if (!g_fallback.providers[i].active)
            continue;
        if (g_interrupted)
            break;

        term_color(T_YELLOW, " [fallback → %s]\n", g_fallback.providers[i].base_url);

        config_t fb_cfg = *primary_cfg;
        fb_cfg.provider = g_fallback.providers[i].type;
        fb_cfg.compat_api_key = g_fallback.providers[i].api_key;
        fb_cfg.compat_base_url = g_fallback.providers[i].base_url;
        if (g_fallback.providers[i].model)
            fb_cfg.model = g_fallback.providers[i].model;

        fallback_wrapper_t fw = {
            .real_cb = cb, .real_ud = ud, .had_error = false, .had_success = false};

        rc = api_stream_request(msgs, n, sys, &fb_cfg, tools, nt, fallback_intercept_cb, &fw);

        if (rc == 0 && !fw.had_error)
            return 0;
        if (fw.had_success)
            return rc;

        if (fw.had_error && !is_fallback_worthy_error(fw.error_msg)) {
            stream_event_t err_ev = {.type = STREAM_EVENT_ERROR, .error_message = fw.error_msg};
            cb(&err_ev, ud);
            return rc;
        }
    }

    return rc;
}

int multi_provider_load_from_env(void) {
    /* Fallback providers are loaded from config.json only. */
    return 0;
}

int multi_provider_load_from_config(const cJSON *root) {
    if (!root)
        return 0;
    int added = 0;

    const cJSON *fallbacks = cJSON_GetObjectItem(root, "fallback_providers");
    if (!fallbacks) {
        const cJSON *fb = cJSON_GetObjectItem(root, "fallback");
        if (fb) {
            const char *url = cJSON_GetStringValue(cJSON_GetObjectItem(fb, "base_url"));
            const char *key = cJSON_GetStringValue(cJSON_GetObjectItem(fb, "api_key"));
            const char *model = cJSON_GetStringValue(cJSON_GetObjectItem(fb, "model"));
            if (url && key) {
                multi_provider_add(url, key, model, PROVIDER_COMPAT);
                added++;
            }
        }
        return added;
    }

    int n = cJSON_GetArraySize(fallbacks);
    for (int i = 0; i < n && g_fallback.count < MAX_FALLBACK_PROVIDERS; i++) {
        const cJSON *item = cJSON_GetArrayItem(fallbacks, i);
        const char *url = cJSON_GetStringValue(cJSON_GetObjectItem(item, "base_url"));
        const char *key = cJSON_GetStringValue(cJSON_GetObjectItem(item, "api_key"));
        const char *model = cJSON_GetStringValue(cJSON_GetObjectItem(item, "model"));
        const char *type_s = cJSON_GetStringValue(cJSON_GetObjectItem(item, "type"));
        provider_type_t type = PROVIDER_COMPAT;
        if (type_s && type_s[0])
            nc_provider_parse(type_s, &type);
        if (url && key) {
            multi_provider_add(url, key, model, type);
            added++;
        }
    }
    return added;
}

char *multi_provider_format(void) {
    if (g_fallback.count == 0)
        return nc_xstrdup("No fallback providers configured.");

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "Fallback providers:\n");

    for (int i = 0; i < g_fallback.count; i++) {
        char line[NC_MSG_MAX];
        snprintf(line, sizeof(line), "  %d. %s%s [%s]%s\n", i + 1, g_fallback.providers[i].base_url,
                 g_fallback.providers[i].model ? "" : "",
                 nc_provider_slug(g_fallback.providers[i].type),
                 g_fallback.providers[i].active ? "" : " (disabled)");
        strbuf_append_str(&sb, line);
        if (g_fallback.providers[i].model) {
            snprintf(line, sizeof(line), "     model: %s\n", g_fallback.providers[i].model);
            strbuf_append_str(&sb, line);
        }
    }
    return strbuf_detach(&sb);
}
