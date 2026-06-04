/*
 * team_memory_sync.c — Cross-agent memory synchronization with secret guard.
 *
 * Enables sharing memories between agents in a team while scanning for
 * leaked secrets before any memory content is shared or persisted.
 */

#include "nevercode.h"

/* TMS_MAX_ENTRIES, TMS_MAX_SECRET_RULES, TMS_KEY_MAX in defaults.h */

typedef struct {
    char *key;
    char *value;
    char *source_agent;
    long timestamp;
    bool synced;
} tms_entry_t;

typedef struct {
    char *label;
    char *pattern;
} secret_rule_t;

static struct {
    tms_entry_t entries[TMS_MAX_ENTRIES];
    int count;
    secret_rule_t rules[TMS_MAX_SECRET_RULES];
    int rule_count;
    int total_synced;
    int total_blocked;
    bool enabled;
} g_tms;

void team_memory_sync_init(void) {
    memset(&g_tms, 0, sizeof(g_tms));
    g_tms.enabled = true;
}

void team_memory_sync_free(void) {
    for (int i = 0; i < g_tms.count; i++) {
        free(g_tms.entries[i].key);
        free(g_tms.entries[i].value);
        free(g_tms.entries[i].source_agent);
    }
    for (int i = 0; i < g_tms.rule_count; i++) {
        free(g_tms.rules[i].label);
        free(g_tms.rules[i].pattern);
    }
    memset(&g_tms, 0, sizeof(g_tms));
}

void team_memory_sync_set_enabled(bool on) {
    g_tms.enabled = on;
}

bool team_memory_sync_enabled(void) {
    return g_tms.enabled;
}

int team_memory_add_secret_rule(const char *label, const char *pattern) {
    if (!label || !pattern || g_tms.rule_count >= TMS_MAX_SECRET_RULES)
        return -1;
    secret_rule_t *r = &g_tms.rules[g_tms.rule_count++];
    r->label = nc_xstrdup(label);
    r->pattern = nc_xstrdup(pattern);
    return 0;
}

static void _load_default_rules(void) {
    if (g_tms.rule_count > 0)
        return;
    team_memory_add_secret_rule("aws-key", "AKIA");
    team_memory_add_secret_rule("gcp-key", "AIza");
    team_memory_add_secret_rule("github-pat", "ghp_");
    team_memory_add_secret_rule("github-pat-fine", "github_pat_");
    team_memory_add_secret_rule("gitlab-pat", "glpat-");
    team_memory_add_secret_rule("slack-token", "xox");
    team_memory_add_secret_rule("stripe-key", "sk_live_");
    team_memory_add_secret_rule("stripe-key-test", "sk_test_");
    team_memory_add_secret_rule("api-key", "sk-");
    team_memory_add_secret_rule("npm-token", "npm_");
    team_memory_add_secret_rule("pypi-token", "pypi-");
}

bool team_memory_scan_secrets(const char *text) {
    if (!text)
        return false;
    _load_default_rules();

    for (int i = 0; i < g_tms.rule_count; i++) {
        if (strstr(text, g_tms.rules[i].pattern)) {
            return true;
        }
    }
    return false;
}

int team_memory_sync_put(const char *key, const char *value, const char *source_agent) {
    if (!key || !value || !g_tms.enabled)
        return -1;

    if (team_memory_scan_secrets(value)) {
        g_tms.total_blocked++;
        return -2;
    }

    for (int i = 0; i < g_tms.count; i++) {
        if (strcmp(g_tms.entries[i].key, key) == 0) {
            free(g_tms.entries[i].value);
            free(g_tms.entries[i].source_agent);
            g_tms.entries[i].value = nc_xstrdup(value);
            g_tms.entries[i].source_agent = source_agent ? nc_xstrdup(source_agent) : NULL;
            g_tms.entries[i].timestamp = nc_time_now();
            g_tms.entries[i].synced = false;
            return 0;
        }
    }

    if (g_tms.count >= TMS_MAX_ENTRIES)
        return -1;

    tms_entry_t *e = &g_tms.entries[g_tms.count++];
    e->key = nc_xstrdup(key);
    e->value = nc_xstrdup(value);
    e->source_agent = source_agent ? nc_xstrdup(source_agent) : NULL;
    e->timestamp = nc_time_now();
    e->synced = false;
    return 0;
}

const char *team_memory_sync_get(const char *key) {
    if (!key)
        return NULL;
    for (int i = 0; i < g_tms.count; i++)
        if (strcmp(g_tms.entries[i].key, key) == 0)
            return g_tms.entries[i].value;
    return NULL;
}

int team_memory_sync_mark_synced(const char *key) {
    for (int i = 0; i < g_tms.count; i++) {
        if (strcmp(g_tms.entries[i].key, key) == 0) {
            g_tms.entries[i].synced = true;
            g_tms.total_synced++;
            return 0;
        }
    }
    return -1;
}

int team_memory_sync_unsynced_count(void) {
    int n = 0;
    for (int i = 0; i < g_tms.count; i++)
        if (!g_tms.entries[i].synced)
            n++;
    return n;
}

int team_memory_sync_count(void) {
    return g_tms.count;
}

int team_memory_sync_blocked_count(void) {
    return g_tms.total_blocked;
}

int team_memory_sync_rule_count(void) {
    _load_default_rules();
    return g_tms.rule_count;
}

char *team_memory_sync_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Team Memory: entries=%d synced=%d blocked=%d rules=%d\n", g_tms.count,
                   g_tms.total_synced, g_tms.total_blocked, g_tms.rule_count);
    for (int i = 0; i < g_tms.count; i++) {
        tms_entry_t *e = &g_tms.entries[i];
        strbuf_appendf(&sb, "  %s: src=%s synced=%s\n", e->key,
                       e->source_agent ? e->source_agent : "local", e->synced ? "yes" : "no");
    }
    return strbuf_detach(&sb);
}
