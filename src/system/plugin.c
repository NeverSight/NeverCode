/*
 * plugin.c — Dynamic plugin loading and lifecycle
 *
 * Loads .so/.dylib plugins from disk, manages registration,
 * initialization, and event dispatch.  Plugins receive lifecycle
 * events (startup, shutdown, turn, tool, compact, handoff, error).
 */

#include "nevercode.h"
#include <dlfcn.h>
#include <sys/stat.h>

static plugin_registry_t g_plugins;
static bool g_plugins_initialized = false;

/* Refusal predicate for plugin shared objects: only regular files owned by no
 * looser-than-owner group may be dlopen'd. Refuses symlinks, directories,
 * fifos/sockets/devices, and group/world-writable files. Returns 0 if the
 * path is loadable, -1 otherwise. The containing directory must not be
 * world-writable either (unless sticky, like /tmp). */
int plugin_file_loadable(const char *path) {
    if (!path || !path[0])
        return -1;

    struct stat st;
    if (lstat(path, &st) != 0)
        return -1;
    if (!S_ISREG(st.st_mode))
        return -1;
    if (st.st_mode & (S_IWGRP | S_IWOTH))
        return -1;

    const char *slash = strrchr(path, '/');
    if (slash) {
        char dir[NC_PATH_MAX];
        size_t dl = (size_t)(slash - path);
        if (dl >= sizeof(dir))
            return -1;
        memcpy(dir, path, dl);
        dir[dl] = '\0';
        struct stat dst;
        if (stat(dir, &dst) == 0 && (dst.st_mode & S_IWOTH) && !(dst.st_mode & S_ISVTX))
            return -1;
    }
    return 0;
}

/* Project-local plugin directories (i.e. .nevercode/plugins inside a repo) are
 * only loaded when explicitly opted in via NEVERCODE_PROJECT_PLUGINS=1.
 * Loading code from a freshly cloned repository otherwise executes unreviewed
 * native code at startup. */
bool plugin_project_plugins_allowed(void) {
    const char *v = getenv("NEVERCODE_PROJECT_PLUGINS");
    return v && (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "on") == 0);
}

void plugin_registry_init(plugin_registry_t *r) {
    memset(r, 0, sizeof(*r));
    r->enabled = true;
}

void plugin_registry_free(plugin_registry_t *r) {
    for (int i = 0; i < r->count; i++) {
        plugin_entry_t *e = &r->plugins[i];
        if (e->state == PLUGIN_LOADED && e->unload_fn)
            e->unload_fn(e->ctx);
        if (e->dl_handle)
            dlclose(e->dl_handle);
        free(e->name);
        free(e->path);
        free(e->version);
        free(e->description);
    }
    r->count = 0;
}

plugin_registry_t *plugin_registry_global(void) {
    if (!g_plugins_initialized) {
        plugin_registry_init(&g_plugins);
        g_plugins_initialized = true;
    }
    return &g_plugins;
}

static int find_plugin(const plugin_registry_t *r, const char *name) {
    for (int i = 0; i < r->count; i++)
        if (r->plugins[i].name && strcmp(r->plugins[i].name, name) == 0)
            return i;
    return -1;
}

int plugin_register(plugin_registry_t *r, const char *name, plugin_init_fn init,
                    plugin_unload_fn unload, plugin_hook_fn hook) {
    if (!r || !name || r->count >= MAX_PLUGINS)
        return -1;
    if (find_plugin(r, name) >= 0)
        return -1;

    plugin_entry_t *e = &r->plugins[r->count];
    memset(e, 0, sizeof(*e));
    e->name = nc_xstrdup(name);
    if (!e->name)
        return -1;
    e->init_fn = init;
    e->unload_fn = unload;
    e->hook_fn = hook;
    e->state = PLUGIN_REGISTERED;
    r->count++;
    return 0;
}

int plugin_load(plugin_registry_t *r, const char *name) {
    int idx = find_plugin(r, name);
    if (idx < 0)
        return -1;

    plugin_entry_t *e = &r->plugins[idx];
    if (e->state == PLUGIN_LOADED)
        return 0;

    if (e->init_fn) {
        e->ctx = e->init_fn();
    }
    e->state = PLUGIN_LOADED;
    return 0;
}

int plugin_unload(plugin_registry_t *r, const char *name) {
    int idx = find_plugin(r, name);
    if (idx < 0)
        return -1;

    plugin_entry_t *e = &r->plugins[idx];
    if (e->state != PLUGIN_LOADED)
        return -1;

    if (e->unload_fn)
        e->unload_fn(e->ctx);
    e->ctx = NULL;
    e->state = PLUGIN_UNLOADED;
    return 0;
}

int plugin_load_so(plugin_registry_t *r, const char *path) {
    if (!r || !path || r->count >= MAX_PLUGINS)
        return -1;

    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        return -1;

    plugin_init_fn init = (plugin_init_fn)dlsym(handle, "plugin_init");
    plugin_unload_fn unload = (plugin_unload_fn)dlsym(handle, "plugin_unload");
    plugin_hook_fn hook = (plugin_hook_fn)dlsym(handle, "plugin_hook");

    const char *(*get_name)(void) = (const char *(*)(void))dlsym(handle, "plugin_name");
    const char *(*get_ver)(void) = (const char *(*)(void))dlsym(handle, "plugin_version");
    const char *(*get_desc)(void) = (const char *(*)(void))dlsym(handle, "plugin_description");

    const char *name = get_name ? get_name() : NULL;
    if (!name || !name[0])
        name = path;

    const char *ver_src = NC_PLUGIN_VERSION_DEFAULT;
    if (get_ver) {
        const char *v = get_ver();
        if (v && v[0])
            ver_src = v;
    }

    char *name_dup = nc_xstrdup(name);
    char *path_dup = nc_xstrdup(path);
    char *ver_dup = nc_xstrdup(ver_src);
    if (!name_dup || !path_dup || !ver_dup) {
        free(name_dup);
        free(path_dup);
        free(ver_dup);
        dlclose(handle);
        return -1;
    }

    char *desc_dup = NULL;
    if (get_desc) {
        const char *d = get_desc();
        if (d) {
            desc_dup = nc_xstrdup(d);
            if (!desc_dup) {
                free(name_dup);
                free(path_dup);
                free(ver_dup);
                dlclose(handle);
                return -1;
            }
        }
    }

    plugin_entry_t *e = &r->plugins[r->count];
    memset(e, 0, sizeof(*e));
    e->name = name_dup;
    e->path = path_dup;
    e->version = ver_dup;
    e->description = desc_dup;
    e->dl_handle = handle;
    e->init_fn = init;
    e->unload_fn = unload;
    e->hook_fn = hook;
    e->state = PLUGIN_REGISTERED;
    r->count++;

    return plugin_load(r, e->name);
}

int plugin_load_dir(plugin_registry_t *r, const char *dir) {
    if (!dir || !dir[0])
        return 0;

    /* Refuse to discover plugins from a world-writable, non-sticky directory */
    struct stat dst;
    if (stat(dir, &dst) == 0 && (dst.st_mode & S_IWOTH) && !(dst.st_mode & S_ISVTX))
        return 0;

    DIR *d = opendir(dir);
    if (!d)
        return 0;

    int loaded = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        bool is_so = str_has_suffix(ent->d_name, ".so");
#ifdef __APPLE__
        bool is_dylib = str_has_suffix(ent->d_name, ".dylib");
#else
        bool is_dylib = false;
#endif
        if (!is_so && !is_dylib)
            continue;

        char path[NC_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (plugin_file_loadable(path) != 0) {
            fprintf(stderr, "[plugin] refusing to load %s: not a regular file or writable by others\n",
                    path);
            continue;
        }
        if (plugin_load_so(r, path) == 0)
            loaded++;
    }
    closedir(d);
    return loaded;
}

int plugin_dispatch(plugin_registry_t *r, plugin_event_t event, const char *data, char **response) {
    if (!r || !r->enabled)
        return 0;

    int handled = 0;
    for (int i = 0; i < r->count; i++) {
        plugin_entry_t *e = &r->plugins[i];
        if (e->state != PLUGIN_LOADED || !e->hook_fn)
            continue;
        int rc = e->hook_fn(event, data, response, e->ctx);
        if (rc > 0)
            handled++;
    }
    return handled;
}

int plugin_count(const plugin_registry_t *r) {
    return r ? r->count : 0;
}

int plugin_loaded_count(const plugin_registry_t *r) {
    if (!r)
        return 0;
    int n = 0;
    for (int i = 0; i < r->count; i++)
        if (r->plugins[i].state == PLUGIN_LOADED)
            n++;
    return n;
}

const char *plugin_state_name(plugin_state_t s) {
    switch (s) {
    case PLUGIN_REGISTERED:
        return "registered";
    case PLUGIN_LOADED:
        return "loaded";
    case PLUGIN_UNLOADED:
        return "unloaded";
    case PLUGIN_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

const char *plugin_event_name(plugin_event_t ev) {
    switch (ev) {
    case PLUGIN_ON_STARTUP:
        return "startup";
    case PLUGIN_ON_SHUTDOWN:
        return "shutdown";
    case PLUGIN_ON_TURN_START:
        return "turn_start";
    case PLUGIN_ON_TURN_END:
        return "turn_end";
    case PLUGIN_ON_TOOL_PRE:
        return "tool_pre";
    case PLUGIN_ON_TOOL_POST:
        return "tool_post";
    case PLUGIN_ON_COMPACT:
        return "compact";
    case PLUGIN_ON_HANDOFF:
        return "handoff";
    case PLUGIN_ON_ERROR:
        return "error";
    case PLUGIN_ON_CUSTOM:
        return "custom";
    default:
        return "unknown";
    }
}

char *plugin_format_list(const plugin_registry_t *r) {
    if (!r || r->count == 0)
        return nc_xstrdup("No plugins loaded.\n");

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Plugins (%d):\n", r->count);

    for (int i = 0; i < r->count; i++) {
        const plugin_entry_t *e = &r->plugins[i];
        strbuf_appendf(&sb, "  [%d] %s", i + 1, e->name ? e->name : "(unnamed)");
        if (e->version)
            strbuf_appendf(&sb, " v%s", e->version);
        strbuf_appendf(&sb, " -- %s", plugin_state_name(e->state));
        if (e->description)
            strbuf_appendf(&sb, " (%s)", e->description);
        strbuf_append_str(&sb, "\n");
    }
    return strbuf_detach(&sb);
}

int plugin_discover_defaults(plugin_registry_t *r, const char *project_root) {
    int n = 0;
    char p[NC_PATH_MAX];
    const char *h = nc_home_dir();

    if (h) {
        snprintf(p, sizeof p, "%s%s/plugins", h, CONFIG_DIR_PRIMARY);
        n += plugin_load_dir(r, p);
        snprintf(p, sizeof p, "%s%s/plugins", h, CONFIG_DIR_COMPAT);
        n += plugin_load_dir(r, p);
    }

    if (project_root && project_root[0]) {
        if (!plugin_project_plugins_allowed()) {
            fprintf(stderr,
                    "[plugin] skipping project-local plugins in %s (set NEVERCODE_PROJECT_PLUGINS=1 to enable)\n",
                    project_root);
        } else {
            snprintf(p, sizeof p, "%s%s/plugins", project_root, CONFIG_DIR_PRIMARY);
            n += plugin_load_dir(r, p);
            snprintf(p, sizeof p, "%s%s/plugins", project_root, CONFIG_DIR_COMPAT);
            n += plugin_load_dir(r, p);
        }
    }
    return n;
}
