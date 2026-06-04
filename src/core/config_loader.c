/*
 * config_loader.c — merge JSON from project and user layers
 */

#include "nevercode.h"

/* NC_MAX_LAYERS in defaults.h */

typedef struct {
    char path[NC_PATH_MAX];
    long mtime;
} layer_meta_t;

static struct {
    cJSON *merged;
    layer_meta_t layers[NC_MAX_LAYERS];
    int nlayer;
    bool stale;
} g;

static long file_mtime(const char *p) {
    struct stat st;
    if (stat(p, &st) != 0)
        return -1;
    return (long)st.st_mtime;
}

static void merge_obj(cJSON *dst, const cJSON *src) {
    if (!dst || !src || src->type != cJSON_Object)
        return;
    const cJSON *ch = src->child;
    while (ch) {
        cJSON *ex = cJSON_GetObjectItemCaseSensitive(dst, ch->string);
        if (ch->type == cJSON_Object && ex && ex->type == cJSON_Object) {
            merge_obj(ex, ch);
        } else {
            cJSON *dup = cJSON_Duplicate(ch, 1);
            if (dup) {
                cJSON_DeleteItemFromObject(dst, ch->string);
                cJSON_AddItemToObject(dst, ch->string, dup);
            }
        }
        ch = ch->next;
    }
}

static int push_layer(const char *path, cJSON *root) {
    if (g.nlayer >= NC_MAX_LAYERS || !root)
        return -1;
    nc_strlcpy(g.layers[g.nlayer].path, path, sizeof g.layers[g.nlayer].path);
    g.layers[g.nlayer].mtime = file_mtime(path);
    if (!g.merged)
        g.merged = cJSON_CreateObject();
    merge_obj(g.merged, root);
    g.nlayer++;
    return 0;
}

static int try_file(const char *path) {
    size_t fsz = 0;
    char *buf = strbuf_read_file(path, &fsz);
    if (!buf)
        return 0;
    if (fsz > NC_CONFIG_MAX_SIZE) {
        free(buf);
        return -1;
    }
    cJSON *j = cJSON_Parse(buf);
    free(buf);
    if (!j || j->type != cJSON_Object) {
        if (j)
            cJSON_Delete(j);
        return -1;
    }
    push_layer(path, j);
    cJSON_Delete(j);
    return 1;
}

void config_loader_init(void) {
    config_loader_free();
    g.stale = false;
}

void config_loader_free(void) {
    if (g.merged)
        cJSON_Delete(g.merged);
    memset(&g, 0, sizeof g);
}

int config_loader_load_all(const char *project_root) {
    config_loader_free();
    if (!project_root || !project_root[0])
        return -1;
    char p[NC_PATH_MAX];
    int n = 0;

    const char *h = nc_home_dir();
    if (h) {
        snprintf(p, sizeof p, "%s%s%s", h, CONFIG_DIR_PRIMARY, NC_LAYER_CONFIG_FILE);
        n += try_file(p) > 0;

        snprintf(p, sizeof p, "%s%s%s", h, CONFIG_DIR_SECONDARY, NC_LAYER_CONFIG_FILE);
        n += try_file(p) > 0;

        snprintf(p, sizeof p, "%s%s%s", h, CONFIG_DIR_COMPAT, NC_LAYER_CONFIG_FILE);
        n += try_file(p) > 0;
    }

    snprintf(p, sizeof p, "%s%s%s", project_root, CONFIG_DIR_SECONDARY, NC_LAYER_CONFIG_FILE);
    n += try_file(p) > 0;

    snprintf(p, sizeof p, "%s%s%s", project_root, CONFIG_DIR_PRIMARY, NC_LAYER_CONFIG_FILE);
    n += try_file(p) > 0;

    snprintf(p, sizeof p, "%s%s%s", project_root, CONFIG_DIR_COMPAT, NC_LAYER_CONFIG_FILE);
    n += try_file(p) > 0;

    g.stale = false;
    return n;
}

cJSON *config_loader_merged(void) {
    return g.merged;
}

int config_loader_get_int(const char *key, int fallback) {
    if (!g.merged || !key)
        return fallback;
    cJSON *j = cJSON_GetObjectItemCaseSensitive(g.merged, key);
    if (!j)
        return fallback;
    if (cJSON_IsNumber(j))
        return j->valueint;
    if (cJSON_IsString(j) && j->valuestring)
        return nc_safe_atoi(j->valuestring, 0);
    return fallback;
}

int config_loader_reload_if_stale(const char *project_root) {
    if (!g.nlayer)
        return config_loader_load_all(project_root);
    for (int i = 0; i < g.nlayer; i++) {
        long m = file_mtime(g.layers[i].path);
        if (m < 0 || m != g.layers[i].mtime) {
            g.stale = true;
            return config_loader_load_all(project_root);
        }
    }
    return 0;
}

char *config_loader_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "ConfigLoader: layers=%d\n", g.nlayer);
    for (int i = 0; i < g.nlayer; i++)
        strbuf_appendf(&sb, "  %s\n", g.layers[i].path);
    if (g.merged) {
        char *j = cJSON_PrintUnformatted(g.merged);
        if (j) {
            strbuf_appendf(&sb, "merged: %s\n", j);
            free(j);
        }
    }
    return strbuf_detach(&sb);
}
