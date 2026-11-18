/*
 * plugin_market.c — Plugin marketplace discovery and resolution
 *
 * Discovers plugins from marketplace manifests (JSON files in
 * standard locations), resolves source paths, and integrates with
 * the plugin registry for installation.
 */

#include "nevercode.h"
#include "defaults.h"

/* MARKET_MANIFEST_FILENAME, MARKET_MAX_MARKETS, MARKET_MAX_PLUGINS in defaults.h */
#define MARKET_MANIFEST_NAME MARKET_MANIFEST_FILENAME
#define MARKET_NAME_MAX PM_NAME_MAX

typedef enum { MARKET_SRC_LOCAL, MARKET_SRC_GIT, MARKET_SRC_URL } market_source_kind_t;

typedef struct {
    char name[MARKET_NAME_MAX];
    market_source_kind_t kind;
    char path[NC_PATH_MAX];
    char sha256[65]; /* required for url/git sources (integrity metadata) */
    char version[NC_SHORT_MAX];
    bool auto_install;
    bool enabled;
} market_plugin_t;

typedef struct {
    char name[MARKET_NAME_MAX];
    char path[NC_PATH_MAX];
    market_plugin_t plugins[MARKET_MAX_PLUGINS];
    int plugin_count;
} marketplace_t;

typedef struct {
    marketplace_t markets[MARKET_MAX_MARKETS];
    int count;
    int total_plugins;
    int errors;
} market_list_t;

static market_list_t g_market;

static bool _valid_sha256_hex(const char *s) {
    if (!s)
        return false;
    size_t n = strlen(s);
    if (n != 64)
        return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex)
            return false;
    }
    return true;
}

/* Remote sources must declare a sha256 so any installer can verify artifacts
 * before execution. Fail-closed: an entry that cannot be verified is dropped. */
static int _remote_sha(const cJSON *src, market_plugin_t *p) {
    const cJSON *sha = cJSON_GetObjectItem(src, "sha256");
    if (!sha || !cJSON_IsString(sha) || !_valid_sha256_hex(sha->valuestring)) {
        g_market.errors++;
        return -1;
    }
    nc_strlcpy(p->sha256, sha->valuestring, sizeof(p->sha256));
    return 0;
}

static bool _remote_scheme_ok(const char *s, bool allow_git) {
    if (strncasecmp(s, "https://", 8) == 0)
        return true;
    if (allow_git && (strncasecmp(s, "ssh://", 6) == 0 || strncasecmp(s, "git@", 4) == 0))
        return true;
    return false;
}

static int parse_source(const cJSON *src, market_plugin_t *p) {
    if (!src)
        return -1;

    const cJSON *local = cJSON_GetObjectItem(src, "local");
    const cJSON *git = cJSON_GetObjectItem(src, "git");
    const cJSON *url = cJSON_GetObjectItem(src, "url");

    if (local && cJSON_IsString(local)) {
        p->kind = MARKET_SRC_LOCAL;
        nc_strlcpy(p->path, local->valuestring, sizeof(p->path));
        return 0;
    }
    if (git && cJSON_IsString(git)) {
        if (!_remote_scheme_ok(git->valuestring, true))
            return -1;
        if (_remote_sha(src, p) != 0)
            return -1;
        p->kind = MARKET_SRC_GIT;
        nc_strlcpy(p->path, git->valuestring, sizeof(p->path));
        return 0;
    }
    if (url && cJSON_IsString(url)) {
        if (!_remote_scheme_ok(url->valuestring, false))
            return -1; /* cleartext http refused */
        if (_remote_sha(src, p) != 0)
            return -1;
        p->kind = MARKET_SRC_URL;
        nc_strlcpy(p->path, url->valuestring, sizeof(p->path));
        return 0;
    }
    return -1;
}

static int load_manifest(const char *dir, marketplace_t *m) {
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%.2045s/%.2045s", dir, MARKET_MANIFEST_NAME);

    char *buf = strbuf_read_file_max(path, NULL, NC_READ_CAP_JSON_MANIFEST);
    if (!buf)
        return -1;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
        return -1;

    nc_strlcpy(m->path, dir, sizeof(m->path));

    const cJSON *name = cJSON_GetObjectItem(root, "name");
    if (name && cJSON_IsString(name))
        nc_strlcpy(m->name, name->valuestring, sizeof(m->name));
    else
        snprintf(m->name, sizeof(m->name), "marketplace");

    const cJSON *plugins = cJSON_GetObjectItem(root, "plugins");
    if (plugins && cJSON_IsArray(plugins)) {
        const cJSON *pl;
        cJSON_ArrayForEach(pl, plugins) {
            if (m->plugin_count >= MARKET_MAX_PLUGINS)
                break;

            market_plugin_t *p = &m->plugins[m->plugin_count];
            memset(p, 0, sizeof(*p));

            const cJSON *pname = cJSON_GetObjectItem(pl, "name");
            if (pname && cJSON_IsString(pname))
                nc_strlcpy(p->name, pname->valuestring, sizeof(p->name));

            const cJSON *ver = cJSON_GetObjectItem(pl, "version");
            if (ver && cJSON_IsString(ver))
                nc_strlcpy(p->version, ver->valuestring, sizeof(p->version));

            const cJSON *src = cJSON_GetObjectItem(pl, "source");
            if (parse_source(src, p) == 0) {
                p->enabled = true;
                const cJSON *ai = cJSON_GetObjectItem(pl, "auto_install");
                p->auto_install = ai && cJSON_IsTrue(ai);
                m->plugin_count++;
            }
        }
    }

    cJSON_Delete(root);
    return m->plugin_count;
}

int plugin_market_scan(const char *project_root) {
    memset(&g_market, 0, sizeof(g_market));

    const char *home = nc_home_dir();
    char search_dirs[4][NC_PATH_MAX];
    int ndirs = 0;

    if (home) {
        snprintf(search_dirs[ndirs++], NC_PATH_MAX, "%s%s/plugins", home, CONFIG_DIR_PRIMARY);
        snprintf(search_dirs[ndirs++], NC_PATH_MAX, "%s%s/plugins", home, CONFIG_DIR_COMPAT);
    }
    if (project_root && project_root[0]) {
        snprintf(search_dirs[ndirs++], NC_PATH_MAX, "%s%s/plugins", project_root,
                 CONFIG_DIR_PRIMARY);
    }

    for (int i = 0; i < ndirs && g_market.count < MARKET_MAX_MARKETS; i++) {
        marketplace_t *m = &g_market.markets[g_market.count];
        memset(m, 0, sizeof(*m));
        int n = load_manifest(search_dirs[i], m);
        if (n > 0) {
            g_market.total_plugins += n;
            g_market.count++;
        }
    }
    return g_market.total_plugins;
}

int plugin_market_count(void) {
    return g_market.total_plugins;
}

int plugin_market_marketplace_count(void) {
    return g_market.count;
}

const char *plugin_market_plugin_sha256(int market_idx, int plugin_idx) {
    if (market_idx < 0 || market_idx >= g_market.count)
        return NULL;
    if (plugin_idx < 0 || plugin_idx >= g_market.markets[market_idx].plugin_count)
        return NULL;
    const char *sha = g_market.markets[market_idx].plugins[plugin_idx].sha256;
    return sha[0] ? sha : NULL;
}

const char *plugin_market_plugin_name(int market_idx, int plugin_idx) {
    if (market_idx < 0 || market_idx >= g_market.count)
        return NULL;
    if (plugin_idx < 0 || plugin_idx >= g_market.markets[market_idx].plugin_count)
        return NULL;
    return g_market.markets[market_idx].plugins[plugin_idx].name;
}

char *plugin_market_format_list(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    if (g_market.count == 0) {
        strbuf_append_str(&sb, "No plugin marketplaces found.\n");
        return strbuf_detach(&sb);
    }
    for (int i = 0; i < g_market.count; i++) {
        const marketplace_t *m = &g_market.markets[i];
        strbuf_appendf(&sb, "Marketplace: %s (%s)\n", m->name, m->path);
        for (int j = 0; j < m->plugin_count; j++) {
            const market_plugin_t *p = &m->plugins[j];
            strbuf_appendf(&sb, "  [%d] %s v%s (%s)%s\n", j + 1, p->name, p->version,
                           p->kind == MARKET_SRC_LOCAL ? "local"
                           : p->kind == MARKET_SRC_GIT ? "git"
                                                       : "url",
                           p->auto_install ? " [auto]" : "");
        }
    }
    return strbuf_detach(&sb);
}
