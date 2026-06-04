#include "nevercode.h"
#include "defaults.h"
#include "config.h"

typedef enum {
    STAGE_DEV = 0,
    STAGE_EXPERIMENTAL,
    STAGE_STABLE,
    STAGE_DEPRECATED,
    STAGE_REMOVED
} flag_stage_t;

typedef struct {
    const char *key;
    flag_stage_t stage;
    bool default_on;
} flag_spec_t;

static const flag_spec_t FLAG_SPECS[] = {
    {"undo", STAGE_STABLE, false},
    {"shell_tool", STAGE_STABLE, true},
    {"unified_exec", STAGE_STABLE, true},
    {"shell_snapshot", STAGE_STABLE, true},
    {"plugins", STAGE_STABLE, true},
    {"multi_agent", STAGE_STABLE, true},
    {"tool_suggest", STAGE_STABLE, true},
    {"prevent_idle_sleep", STAGE_STABLE, false},
    {"fast_mode", STAGE_STABLE, true},
    {"request_compression", STAGE_STABLE, true},
    {"js_repl", STAGE_EXPERIMENTAL, false},
    {"code_mode", STAGE_EXPERIMENTAL, false},
    {"code_mode_only", STAGE_EXPERIMENTAL, false},
    {"apply_patch_freeform", STAGE_EXPERIMENTAL, false},
    {"guardian_approval", STAGE_EXPERIMENTAL, false},
    {"exec_permission_approvals", STAGE_EXPERIMENTAL, false},
    {"hooks", STAGE_STABLE, true},
    {"memory_tool", STAGE_STABLE, true},
    {"realtime_conversation", STAGE_EXPERIMENTAL, false},
    {"image_generation", STAGE_EXPERIMENTAL, false},
    {"web_search_live", STAGE_EXPERIMENTAL, true},
    {"shell_zsh_fork", STAGE_DEV, false},
    {"multi_agent_v2", STAGE_DEV, false},
    {"spawn_csv", STAGE_DEV, false},
    {"runtime_metrics", STAGE_DEV, false},
    {"artifact", STAGE_DEV, false},
};

#define FLAG_SPEC_COUNT NC_ARRAY_LEN(FLAG_SPECS)

static struct {
    bool overridden[FF_MAX_FEATURES];
    bool values[FF_MAX_FEATURES];
    bool initialized;
} g_flags;

static int spec_index(const char *key) {
    if (!key)
        return -1;
    for (int i = 0; i < (int)FLAG_SPEC_COUNT; i++)
        if (strcmp(FLAG_SPECS[i].key, key) == 0)
            return i;
    return -1;
}

void ff_init(void) {
    memset(&g_flags, 0, sizeof(g_flags));
    for (int i = 0; i < (int)FLAG_SPEC_COUNT; i++)
        g_flags.values[i] = FLAG_SPECS[i].default_on;
    g_flags.initialized = true;
}

bool ff_enabled(const char *key) {
    if (!g_flags.initialized)
        ff_init();
    int i = spec_index(key);
    return i >= 0 && g_flags.values[i];
}

void ff_set(const char *key, bool on) {
    if (!g_flags.initialized)
        ff_init();
    int i = spec_index(key);
    if (i >= 0) {
        g_flags.overridden[i] = true;
        g_flags.values[i] = on;
    }
}

void ff_reset(const char *key) {
    if (!g_flags.initialized)
        ff_init();
    int i = spec_index(key);
    if (i >= 0) {
        g_flags.overridden[i] = false;
        g_flags.values[i] = FLAG_SPECS[i].default_on;
    }
}

void ff_reset_all(void) {
    ff_init();
}
bool ff_is_known(const char *key) {
    return spec_index(key) >= 0;
}

int ff_stage(const char *key) {
    int i = spec_index(key);
    return i >= 0 ? (int)FLAG_SPECS[i].stage : -1;
}

bool ff_default_value(const char *key) {
    int i = spec_index(key);
    return i >= 0 && FLAG_SPECS[i].default_on;
}

int ff_count(void) {
    return (int)FLAG_SPEC_COUNT;
}
const char *ff_key_at(int index) {
    return (index >= 0 && index < (int)FLAG_SPEC_COUNT) ? FLAG_SPECS[index].key : NULL;
}

void ff_apply_map(const char *const *keys, const bool *vals, int n) {
    if (!g_flags.initialized)
        ff_init();
    for (int i = 0; i < n; i++)
        ff_set(keys[i], vals[i]);
}

int ff_enabled_list(const char **out, int cap) {
    if (!g_flags.initialized)
        ff_init();
    int n = 0;
    for (int i = 0; i < (int)FLAG_SPEC_COUNT && n < cap; i++)
        if (g_flags.values[i])
            out[n++] = FLAG_SPECS[i].key;
    return n;
}

void ff_normalize_deps(void) {
    if (!g_flags.initialized)
        ff_init();
    if (ff_enabled("code_mode_only") && !ff_enabled("code_mode"))
        ff_set("code_mode", true);
    if (ff_enabled("spawn_csv") && !ff_enabled("multi_agent"))
        ff_set("multi_agent", true);
}

void ff_load_env(void) {
    if (!g_flags.initialized)
        ff_init();
    const char *val = getenv(FF_ENV_VAR);
    if (!val)
        return;
    char buf[NC_ERRBUF_MAX];
    nc_strlcpy(buf, val, sizeof(buf));
    char *tok = strtok(buf, ",");
    while (tok) {
        while (*tok == ' ')
            tok++;
        bool neg = false;
        if (*tok == '!') {
            neg = true;
            tok++;
        }
        if (*tok)
            ff_set(tok, !neg);
        tok = strtok(NULL, ",");
    }
    ff_normalize_deps();
}

void ff_free(void) {
    memset(&g_flags, 0, sizeof(g_flags));
}
