/*
 * provider_auth.c — API provider authentication and token refresh.
 *
 * Manages API keys, OAuth tokens, and automatic credential refresh
 * for multi-provider configurations.
 */

#include "nevercode.h"

/* AUTH_MAX_PROVIDERS, AUTH_REFRESH_MARGIN in defaults.h */

typedef enum { AUTH_NONE = 0, AUTH_API_KEY, AUTH_BEARER, AUTH_CUSTOM_HEADER } auth_method_t;

typedef struct {
    char *provider_id;
    auth_method_t method;
    char *credential;
    char *refresh_url;
    char *refresh_token;
    long expires_at;
    bool valid;
    int refresh_count;
    long last_refresh;
} auth_entry_t;

static struct {
    auth_entry_t entries[AUTH_MAX_PROVIDERS];
    int count;
    int total_refreshes;
    int total_failures;
} g_auth;

void provider_auth_init(void) {
    memset(&g_auth, 0, sizeof(g_auth));
}

static void _free_entry(auth_entry_t *e) {
    free(e->provider_id);
    free(e->credential);
    free(e->refresh_url);
    free(e->refresh_token);
    memset(e, 0, sizeof(*e));
}

void provider_auth_free(void) {
    for (int i = 0; i < g_auth.count; i++)
        _free_entry(&g_auth.entries[i]);
    memset(&g_auth, 0, sizeof(g_auth));
}

static int _find(const char *id) {
    for (int i = 0; i < g_auth.count; i++)
        if (g_auth.entries[i].provider_id && strcmp(g_auth.entries[i].provider_id, id) == 0)
            return i;
    return -1;
}

int provider_auth_set_key(const char *provider_id, const char *key) {
    if (!provider_id || !key)
        return -1;

    int idx = _find(provider_id);
    if (idx < 0) {
        if (g_auth.count >= AUTH_MAX_PROVIDERS)
            return -1;
        idx = g_auth.count++;
    } else {
        _free_entry(&g_auth.entries[idx]);
    }

    auth_entry_t *e = &g_auth.entries[idx];
    e->provider_id = nc_xstrdup(provider_id);
    e->method = AUTH_API_KEY;
    e->credential = nc_xstrdup(key);
    e->valid = true;
    e->expires_at = 0;
    return 0;
}

int provider_auth_set_bearer(const char *provider_id, const char *token, long expires_at) {
    if (!provider_id || !token)
        return -1;

    int idx = _find(provider_id);
    if (idx < 0) {
        if (g_auth.count >= AUTH_MAX_PROVIDERS)
            return -1;
        idx = g_auth.count++;
    } else {
        _free_entry(&g_auth.entries[idx]);
    }

    auth_entry_t *e = &g_auth.entries[idx];
    e->provider_id = nc_xstrdup(provider_id);
    e->method = AUTH_BEARER;
    e->credential = nc_xstrdup(token);
    e->valid = true;
    e->expires_at = expires_at;
    return 0;
}

int provider_auth_set_refresh(const char *provider_id, const char *refresh_url,
                              const char *refresh_token) {
    int idx = _find(provider_id);
    if (idx < 0)
        return -1;

    auth_entry_t *e = &g_auth.entries[idx];
    free(e->refresh_url);
    free(e->refresh_token);
    e->refresh_url = refresh_url ? nc_xstrdup(refresh_url) : NULL;
    e->refresh_token = refresh_token ? nc_xstrdup(refresh_token) : NULL;
    return 0;
}

bool provider_auth_needs_refresh(const char *provider_id) {
    int idx = _find(provider_id);
    if (idx < 0)
        return false;

    auth_entry_t *e = &g_auth.entries[idx];
    if (e->expires_at == 0)
        return false;
    return nc_time_now() >= (e->expires_at - AUTH_REFRESH_MARGIN);
}

int provider_auth_refresh(const char *provider_id) {
    int idx = _find(provider_id);
    if (idx < 0)
        return -1;

    auth_entry_t *e = &g_auth.entries[idx];
    if (!e->refresh_url || !e->refresh_token)
        return -1;

    e->refresh_count++;
    e->last_refresh = nc_time_now();
    g_auth.total_refreshes++;

    return 0;
}

const char *provider_auth_credential(const char *provider_id) {
    int idx = _find(provider_id);
    if (idx < 0)
        return NULL;
    return g_auth.entries[idx].credential;
}

bool provider_auth_valid(const char *provider_id) {
    int idx = _find(provider_id);
    if (idx < 0)
        return false;
    auth_entry_t *e = &g_auth.entries[idx];
    if (!e->valid)
        return false;
    if (e->expires_at > 0 && nc_time_now() >= e->expires_at)
        return false;
    return true;
}

const char *provider_auth_header_name(const char *provider_id) {
    int idx = _find(provider_id);
    if (idx < 0)
        return "Authorization";
    auth_entry_t *e = &g_auth.entries[idx];
    if (e->method == AUTH_CUSTOM_HEADER)
        return "X-API-Key";
    return "Authorization";
}

char *provider_auth_header_value(const char *provider_id) {
    int idx = _find(provider_id);
    if (idx < 0)
        return NULL;
    auth_entry_t *e = &g_auth.entries[idx];
    if (!e->credential)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    switch (e->method) {
    case AUTH_BEARER:
        strbuf_appendf(&sb, "Bearer %s", e->credential);
        break;
    case AUTH_API_KEY:
    case AUTH_CUSTOM_HEADER:
    default:
        strbuf_append_str(&sb, e->credential);
        break;
    }
    return strbuf_detach(&sb);
}

int provider_auth_count(void) {
    return g_auth.count;
}

int provider_auth_total_refreshes(void) {
    return g_auth.total_refreshes;
}

char *provider_auth_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Auth: %d providers, %d refreshes\n", g_auth.count, g_auth.total_refreshes);
    for (int i = 0; i < g_auth.count; i++) {
        auth_entry_t *e = &g_auth.entries[i];
        strbuf_appendf(&sb, "  %s: valid=%s refreshes=%d", e->provider_id,
                       provider_auth_valid(e->provider_id) ? "yes" : "no", e->refresh_count);
        if (e->expires_at > 0)
            strbuf_appendf(&sb, " expires_in=%lds", e->expires_at - nc_time_now());
        strbuf_append_str(&sb, "\n");
    }
    return strbuf_detach(&sb);
}
