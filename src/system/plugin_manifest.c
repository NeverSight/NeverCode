#include "nevercode.h"

typedef struct {
    char *id, *name, *version, *description, *author, *entry_point;
    char *capabilities[MANIFEST_MAX_CAPS];
    int num_capabilities;
    char *mcp_servers[MANIFEST_MAX_MCP];
    int num_mcp_servers;
    bool verified;
    long load_time;
} manifest_entry_t;

typedef struct {
    manifest_entry_t entries[MANIFEST_MAX_PLUGINS];
    int count;
    char search_dirs[MANIFEST_MAX_SEARCH_DIRS][NC_PATH_MAX];
    int num_search_dirs;
    bool initialized;
} manifest_ctx_t;

static manifest_ctx_t g_manifest;

static void me_free(manifest_entry_t *e) {
    free(e->id);
    free(e->name);
    free(e->version);
    free(e->description);
    free(e->author);
    free(e->entry_point);
    for (int i = 0; i < e->num_capabilities; i++)
        free(e->capabilities[i]);
    for (int i = 0; i < e->num_mcp_servers; i++)
        free(e->mcp_servers[i]);
    memset(e, 0, sizeof(*e));
}

static int me_parse(manifest_entry_t *out, const cJSON *root) {
    memset(out, 0, sizeof(*out));
    const char *id_str = nc_cjson_str(root, "id", NULL);
    const char *nm_str = nc_cjson_str(root, "name", NULL);
    if (!id_str || !nm_str)
        return -1;

    out->id = nc_xstrdup(id_str);
    out->name = nc_xstrdup(nm_str);
    out->version = nc_cjson_dup_str(root, "version", NC_PLUGIN_VERSION_DEFAULT);
    out->description = nc_cjson_dup_str(root, "description", "");
    out->author = nc_cjson_dup_str(root, "author", "");
    out->entry_point = nc_cjson_dup_str(root, "entry_point", "");

    cJSON *caps = cJSON_GetObjectItem(root, "capabilities");
    if (caps && cJSON_IsArray(caps)) {
        cJSON *it;
        cJSON_ArrayForEach(it, caps) {
            if (cJSON_IsString(it) && out->num_capabilities < MANIFEST_MAX_CAPS)
                out->capabilities[out->num_capabilities++] = nc_xstrdup(it->valuestring);
        }
    }

    cJSON *mcps = cJSON_GetObjectItem(root, "mcp_servers");
    if (mcps && cJSON_IsArray(mcps)) {
        cJSON *it;
        cJSON_ArrayForEach(it, mcps) {
            if (cJSON_IsString(it) && out->num_mcp_servers < MANIFEST_MAX_MCP)
                out->mcp_servers[out->num_mcp_servers++] = nc_xstrdup(it->valuestring);
        }
    }

    out->verified = false;
    out->load_time = nc_wall_sec();
    return 0;
}

void plugin_manifest_init(void) {
    memset(&g_manifest, 0, sizeof(g_manifest));
    g_manifest.initialized = true;
}

void plugin_manifest_free(void) {
    for (int i = 0; i < g_manifest.count; i++)
        me_free(&g_manifest.entries[i]);
    g_manifest.count = 0;
    g_manifest.num_search_dirs = 0;
    g_manifest.initialized = false;
}

int plugin_manifest_load_file(const char *path) {
    if (!g_manifest.initialized || !path || g_manifest.count >= MANIFEST_MAX_PLUGINS)
        return -1;
    cJSON *root = nc_json_load(path);
    if (!root)
        return -1;
    int rc = me_parse(&g_manifest.entries[g_manifest.count], root);
    cJSON_Delete(root);
    if (rc == 0)
        g_manifest.count++;
    return rc;
}

int plugin_manifest_discover(const char *dir) {
    if (!g_manifest.initialized || !dir)
        return 0;
    DIR *d = opendir(dir);
    if (!d)
        return 0;

    int loaded = 0;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.')
            continue;
        char sd[NC_PATH_MAX];
        snprintf(sd, sizeof(sd), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(sd, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
        char mp[NC_PATH_MAX];
        snprintf(mp, sizeof(mp), "%.4079s/manifest.json", sd);
        if (access(mp, F_OK) != 0)
            snprintf(mp, sizeof(mp), "%.4081s/plugin.json", sd);
        if (access(mp, F_OK) != 0)
            continue;
        if (plugin_manifest_load_file(mp) == 0)
            loaded++;
    }
    closedir(d);
    return loaded;
}

int plugin_manifest_count(void) {
    return g_manifest.count;
}

int plugin_manifest_find(const char *id) {
    if (!id)
        return -1;
    for (int i = 0; i < g_manifest.count; i++)
        if (strcmp(g_manifest.entries[i].id, id) == 0)
            return i;
    return -1;
}

char *plugin_manifest_format_list(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Plugin Manifests: %d loaded\n", g_manifest.count);
    for (int i = 0; i < g_manifest.count; i++)
        strbuf_appendf(&sb, "  [%d] %s v%s\n", i, g_manifest.entries[i].name,
                       g_manifest.entries[i].version);
    return strbuf_detach(&sb);
}
