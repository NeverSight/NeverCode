/*
 * network_proxy.c — HTTP proxy configuration and application
 *
 * Manages HTTP/HTTPS/SOCKS proxy settings loaded from environment
 * variables, config files, or programmatic configuration.
 * Applies proxy settings to child processes via environment variables.
 */

#include "nevercode.h"

/* NPROXY_URL_MAX, NPROXY_NO_PROXY_MAX, NPROXY_HOST_MAX in defaults.h */

typedef enum { NPROXY_NONE, NPROXY_HTTP, NPROXY_HTTPS, NPROXY_SOCKS5 } nproxy_type_t;

typedef struct {
    nproxy_type_t type;
    char url[NPROXY_URL_MAX];
    char host[NPROXY_HOST_MAX];
    int port;
    char user[NC_TAG_MAX];
    char pass[NC_TAG_MAX];
    bool active;
} nproxy_entry_t;

typedef struct {
    nproxy_entry_t http;
    nproxy_entry_t https;
    nproxy_entry_t socks;
    char no_proxy[NPROXY_NO_PROXY_MAX];
    bool loaded;
} nproxy_state_t;

static nproxy_state_t g_nproxy;

static void parse_proxy_url(const char *url, nproxy_entry_t *entry) {
    if (!url || !url[0])
        return;

    nc_strlcpy(entry->url, url, sizeof(entry->url));
    entry->active = true;

    if (nc_starts_with(url, "socks5://") || nc_starts_with(url, "socks5h://"))
        entry->type = NPROXY_SOCKS5;
    else if (nc_starts_with(url, "https://"))
        entry->type = NPROXY_HTTPS;
    else
        entry->type = NPROXY_HTTP;

    const char *hoststart = strstr(url, "://");
    if (hoststart)
        hoststart += 3;
    else
        hoststart = url;

    const char *at = strchr(hoststart, '@');
    if (at) {
        size_t credlen = (size_t)(at - hoststart);
        char creds[NC_LABEL_MAX];
        if (credlen >= sizeof(creds))
            credlen = sizeof(creds) - 1;
        memcpy(creds, hoststart, credlen);
        creds[credlen] = '\0';
        char *colon = strchr(creds, ':');
        if (colon) {
            *colon = '\0';
            nc_strlcpy(entry->user, creds, sizeof(entry->user));
            nc_strlcpy(entry->pass, colon + 1, sizeof(entry->pass));
        }
        hoststart = at + 1;
    }

    const char *portstart = strrchr(hoststart, ':');
    const char *slash = strchr(hoststart, '/');
    if (portstart && (!slash || portstart < slash)) {
        size_t hlen = (size_t)(portstart - hoststart);
        if (hlen >= sizeof(entry->host))
            hlen = sizeof(entry->host) - 1;
        memcpy(entry->host, hoststart, hlen);
        entry->host[hlen] = '\0';
        entry->port = nc_safe_atoi(portstart + 1, 0);
    } else {
        const char *end = slash ? slash : hoststart + strlen(hoststart);
        size_t hlen = (size_t)(end - hoststart);
        if (hlen >= sizeof(entry->host))
            hlen = sizeof(entry->host) - 1;
        memcpy(entry->host, hoststart, hlen);
        entry->host[hlen] = '\0';
        entry->port =
            (entry->type == NPROXY_HTTPS) ? NPROXY_DEFAULT_HTTPS_PORT : NPROXY_DEFAULT_HTTP_PORT;
    }
}

int nproxy_load_env(void) {
    memset(&g_nproxy, 0, sizeof(g_nproxy));

    const char *hp = getenv(ENV_HTTP_PROXY);
    if (!hp)
        hp = getenv(ENV_HTTP_PROXY_LC);
    parse_proxy_url(hp, &g_nproxy.http);

    const char *hsp = getenv(ENV_HTTPS_PROXY);
    if (!hsp)
        hsp = getenv(ENV_HTTPS_PROXY_LC);
    parse_proxy_url(hsp, &g_nproxy.https);

    const char *sp = getenv(ENV_ALL_PROXY);
    if (!sp)
        sp = getenv(ENV_ALL_PROXY_LC);
    parse_proxy_url(sp, &g_nproxy.socks);

    const char *np = getenv(ENV_NO_PROXY);
    if (!np)
        np = getenv(ENV_NO_PROXY_LC);
    if (np)
        nc_strlcpy(g_nproxy.no_proxy, np, sizeof(g_nproxy.no_proxy));

    g_nproxy.loaded = true;
    return (g_nproxy.http.active || g_nproxy.https.active || g_nproxy.socks.active) ? 1 : 0;
}

int nproxy_set(nproxy_type_t type, const char *url) {
    switch (type) {
    case NPROXY_HTTP:
        parse_proxy_url(url, &g_nproxy.http);
        break;
    case NPROXY_HTTPS:
        parse_proxy_url(url, &g_nproxy.https);
        break;
    case NPROXY_SOCKS5:
        parse_proxy_url(url, &g_nproxy.socks);
        break;
    default:
        return -1;
    }
    return 0;
}

int nproxy_set_no_proxy(const char *hosts) {
    if (!hosts)
        return -1;
    nc_strlcpy(g_nproxy.no_proxy, hosts, sizeof(g_nproxy.no_proxy));
    return 0;
}

const char *nproxy_get_url(nproxy_type_t type) {
    switch (type) {
    case NPROXY_HTTP:
        return g_nproxy.http.active ? g_nproxy.http.url : NULL;
    case NPROXY_HTTPS:
        return g_nproxy.https.active ? g_nproxy.https.url : NULL;
    case NPROXY_SOCKS5:
        return g_nproxy.socks.active ? g_nproxy.socks.url : NULL;
    default:
        return NULL;
    }
}

bool nproxy_is_active(void) {
    return g_nproxy.http.active || g_nproxy.https.active || g_nproxy.socks.active;
}

static bool host_in_no_proxy(const char *host) {
    if (!g_nproxy.no_proxy[0] || !host)
        return false;

    char buf[NPROXY_NO_PROXY_MAX];
    nc_strlcpy(buf, g_nproxy.no_proxy, sizeof(buf));
    char *tok = strtok(buf, ",");
    while (tok) {
        while (*tok == ' ')
            tok++;
        if (strcmp(tok, "*") == 0)
            return true;
        size_t tlen = strlen(tok), hlen = strlen(host);
        if (tlen <= hlen && strcasecmp(host + hlen - tlen, tok) == 0)
            return true;
        tok = strtok(NULL, ",");
    }
    return false;
}

const char *nproxy_resolve_for_host(const char *host) {
    if (host_in_no_proxy(host))
        return NULL;
    if (g_nproxy.https.active)
        return g_nproxy.https.url;
    if (g_nproxy.http.active)
        return g_nproxy.http.url;
    if (g_nproxy.socks.active)
        return g_nproxy.socks.url;
    return NULL;
}

char *nproxy_format_status(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    if (!nproxy_is_active()) {
        strbuf_append_str(&sb, "No proxy configured.\n");
    } else {
        if (g_nproxy.http.active)
            strbuf_appendf(&sb, "HTTP proxy: %s\n", g_nproxy.http.url);
        if (g_nproxy.https.active)
            strbuf_appendf(&sb, "HTTPS proxy: %s\n", g_nproxy.https.url);
        if (g_nproxy.socks.active)
            strbuf_appendf(&sb, "SOCKS proxy: %s\n", g_nproxy.socks.url);
        if (g_nproxy.no_proxy[0])
            strbuf_appendf(&sb, "No-proxy: %s\n", g_nproxy.no_proxy);
    }
    return strbuf_detach(&sb);
}
