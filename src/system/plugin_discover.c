/*
 * plugin_discover.c — Plugin auto-discovery
 *
 * Scans known directories for installable plugins,
 * reads their manifests, and reports capabilities.
 */

#include "nevercode.h"
#include "defaults.h"

/* ── Types ── */

typedef struct {
    char id[TOOL_POOL_NAME_MAX];
    char name[TOOL_POOL_NAME_MAX];
    char description[TERR_DETAIL_MAX];
    bool has_skills;
    bool has_mcp;
    bool installed;
    int mcp_server_count;
} discoverable_plugin_t;

typedef struct {
    discoverable_plugin_t items[MANIFEST_MAX_PLUGINS];
    int count;
} discovery_result_t;

/* ── Scanning ── */

static bool has_manifest(const char *dir_path) {
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/manifest.json", dir_path);
    return nc_file_exists(path);
}

static int read_manifest_info(const char *dir_path, discoverable_plugin_t *out) {
    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/manifest.json", dir_path);

    char *buf = strbuf_read_file_max(path, NULL, NC_READ_CAP_JSON_SMALL);
    if (!buf)
        return -1;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
        return -1;

    cJSON *jid = cJSON_GetObjectItem(root, "id");
    cJSON *jname = cJSON_GetObjectItem(root, "name");
    cJSON *jdesc = cJSON_GetObjectItem(root, "description");
    cJSON *jskill = cJSON_GetObjectItem(root, "has_skills");
    cJSON *jmcp = cJSON_GetObjectItem(root, "mcp_servers");

    if (jid && cJSON_IsString(jid))
        nc_strlcpy(out->id, jid->valuestring, sizeof(out->id));
    if (jname && cJSON_IsString(jname))
        nc_strlcpy(out->name, jname->valuestring, sizeof(out->name));
    if (jdesc && cJSON_IsString(jdesc))
        nc_strlcpy(out->description, jdesc->valuestring, sizeof(out->description));
    out->has_skills = jskill && cJSON_IsTrue(jskill);
    out->has_mcp = (jmcp && cJSON_IsArray(jmcp) && cJSON_GetArraySize(jmcp) > 0);
    if (out->has_mcp)
        out->mcp_server_count = cJSON_GetArraySize(jmcp);

    cJSON_Delete(root);
    return 0;
}

int plugin_discover_scan(const char *search_dir, discovery_result_t *result) {
    if (!search_dir || !result)
        return -1;
    memset(result, 0, sizeof(*result));

    DIR *d = opendir(search_dir);
    if (!d)
        return -1;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && result->count < MANIFEST_MAX_PLUGINS) {
        if (ent->d_name[0] == '.')
            continue;

        char sub[NC_PATH_MAX];
        snprintf(sub, sizeof(sub), "%s/%s", search_dir, ent->d_name);

        struct stat st;
        if (stat(sub, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
        if (!has_manifest(sub))
            continue;

        discoverable_plugin_t *p = &result->items[result->count];
        if (read_manifest_info(sub, p) == 0) {
            result->count++;
        }
    }

    closedir(d);
    return result->count;
}

int plugin_discover_scan_defaults(discovery_result_t *result) {
    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));

    const char *home = config_home_dir();
    if (!home)
        return -1;

    char dir[NC_PATH_MAX];
    const char *subdirs[] = {"plugins", "marketplace", NULL};
    int total = 0;

    for (int i = 0; subdirs[i]; i++) {
        snprintf(dir, sizeof(dir), "%s/%s", home, subdirs[i]);
        discovery_result_t partial;
        int n = plugin_discover_scan(dir, &partial);
        if (n > 0) {
            for (int j = 0; j < partial.count && result->count < MANIFEST_MAX_PLUGINS; j++) {
                result->items[result->count++] = partial.items[j];
            }
            total += n;
        }
    }
    return total;
}

const char *plugin_discover_capability_summary(const discoverable_plugin_t *p) {
    if (!p)
        return "none";
    if (p->has_skills && p->has_mcp)
        return "skills+mcp";
    if (p->has_skills)
        return "skills";
    if (p->has_mcp)
        return "mcp";
    return "basic";
}

int plugin_discover_home_plugin_count(void) {
    discovery_result_t r;
    return plugin_discover_scan_defaults(&r);
}

char *plugin_discover_format(const discovery_result_t *result) {
    if (!result || result->count == 0)
        return nc_xstrdup("No plugins discovered.\n");

    size_t sz = (size_t)result->count * 256 + 128;
    char *buf = malloc(sz);
    if (!buf)
        return NULL;

    int off = snprintf(buf, sz, "Discovered %d plugin(s):\n", result->count);
    for (int i = 0; i < result->count; i++) {
        const discoverable_plugin_t *p = &result->items[i];
        off += snprintf(buf + off, sz - (size_t)off, "  [%d] %s (%s) — %s [%s]\n", i + 1, p->name,
                        p->id, p->description[0] ? p->description : "(no description)",
                        plugin_discover_capability_summary(p));
    }
    return buf;
}
