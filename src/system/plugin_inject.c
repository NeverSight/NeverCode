/*
 * plugin_inject.c — Plugin hook registration into tool pipeline
 *
 * Allows plugins to register tools, hooks, and context
 * providers into the running agent session dynamically.
 */

#include "nevercode.h"
#include "defaults.h"

/* ── Internal types ── */

typedef struct {
    inject_kind_t kind;
    char plugin_id[TOOL_POOL_NAME_MAX];
    char name[TOOL_POOL_NAME_MAX];
    char description[TERR_DETAIL_MAX];
    void *handle; /* opaque pointer to injected resource */
    bool active;
} injection_entry_t;

typedef struct {
    injection_entry_t entries[DYN_TOOL_MAX_ENTRIES];
    int count;
    bool initialized;
} injection_registry_t;

static injection_registry_t g_registry;

/* ── Init / Free ── */

void plugin_inject_init(void) {
    memset(&g_registry, 0, sizeof(g_registry));
    g_registry.initialized = true;
}

void plugin_inject_free(void) {
    g_registry.count = 0;
    g_registry.initialized = false;
}

/* ── Registration ── */

int plugin_inject_register(const char *plugin_id, inject_kind_t kind, const char *name,
                           const char *desc, void *handle) {
    if (!g_registry.initialized)
        return -1;
    if (g_registry.count >= DYN_TOOL_MAX_ENTRIES)
        return -1;
    if (!plugin_id || !name)
        return -1;

    /* Prevent duplicate registrations */
    for (int i = 0; i < g_registry.count; i++) {
        if (strcmp(g_registry.entries[i].plugin_id, plugin_id) == 0 &&
            strcmp(g_registry.entries[i].name, name) == 0 && g_registry.entries[i].kind == kind) {
            return -1;
        }
    }

    injection_entry_t *e = &g_registry.entries[g_registry.count];
    e->kind = kind;
    nc_strlcpy(e->plugin_id, plugin_id, sizeof(e->plugin_id));
    nc_strlcpy(e->name, name, sizeof(e->name));
    if (desc)
        nc_strlcpy(e->description, desc, sizeof(e->description));
    e->handle = handle;
    e->active = true;
    g_registry.count++;
    return 0;
}

/* ── Lookup ── */

int plugin_inject_count(void) {
    return g_registry.count;
}

int plugin_inject_count_by_kind(inject_kind_t kind) {
    int c = 0;
    for (int i = 0; i < g_registry.count; i++)
        if (g_registry.entries[i].kind == kind && g_registry.entries[i].active)
            c++;
    return c;
}

void *plugin_inject_find(const char *name, inject_kind_t kind) {
    if (!name)
        return NULL;
    for (int i = 0; i < g_registry.count; i++) {
        if (g_registry.entries[i].kind == kind && g_registry.entries[i].active &&
            strcmp(g_registry.entries[i].name, name) == 0)
            return g_registry.entries[i].handle;
    }
    return NULL;
}

/* ── Deregistration ── */

int plugin_inject_remove(const char *plugin_id, const char *name, inject_kind_t kind) {
    if (!plugin_id || !name)
        return -1;
    for (int i = 0; i < g_registry.count; i++) {
        injection_entry_t *e = &g_registry.entries[i];
        if (e->kind == kind && strcmp(e->plugin_id, plugin_id) == 0 && strcmp(e->name, name) == 0) {
            if (i < g_registry.count - 1)
                g_registry.entries[i] = g_registry.entries[g_registry.count - 1];
            g_registry.count--;
            return 0;
        }
    }
    return -1;
}

int plugin_inject_remove_all(const char *plugin_id) {
    if (!plugin_id)
        return -1;
    int removed = 0;
    for (int i = g_registry.count - 1; i >= 0; i--) {
        if (strcmp(g_registry.entries[i].plugin_id, plugin_id) == 0) {
            if (i < g_registry.count - 1)
                g_registry.entries[i] = g_registry.entries[g_registry.count - 1];
            g_registry.count--;
            removed++;
        }
    }
    return removed;
}

/* ── Format ── */

static const char *inject_kind_name(inject_kind_t k) {
    switch (k) {
    case INJECT_TOOL:
        return "tool";
    case INJECT_HOOK:
        return "hook";
    case INJECT_CONTEXT:
        return "context";
    case INJECT_SKILL:
        return "skill";
    }
    return "unknown";
}

char *plugin_inject_format(void) {
    if (g_registry.count == 0)
        return nc_xstrdup("No injected resources.\n");

    size_t sz = (size_t)g_registry.count * 200 + 128;
    char *buf = malloc(sz);
    if (!buf)
        return NULL;

    int off = snprintf(buf, sz, "Injected resources (%d):\n", g_registry.count);
    for (int i = 0; i < g_registry.count; i++) {
        const injection_entry_t *e = &g_registry.entries[i];
        off +=
            snprintf(buf + off, sz - (size_t)off, "  [%s] %s.%s — %s%s\n",
                     inject_kind_name(e->kind), e->plugin_id, e->name,
                     e->description[0] ? e->description : "(none)", e->active ? "" : " [disabled]");
    }
    return buf;
}
