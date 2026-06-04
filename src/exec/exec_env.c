/*
 * exec_env.c — Normalize subprocess environment for deterministic tool output
 *
 * Sets a controlled set of environment variables before spawning tool
 * subprocesses so that output is deterministic regardless of the user's
 * terminal configuration.  This avoids color codes, locale-dependent
 * formatting, and interactive pager invocations polluting tool results.
 */

#include "nevercode.h"
#include "defaults.h"

typedef struct {
    const char *key;
    const char *value;
} env_pair_t;

static const env_pair_t EXEC_ENV[] = {
    {"NO_COLOR", "1"},       {"TERM", "dumb"},      {"LANG", "C.UTF-8"},
    {"LC_CTYPE", "C.UTF-8"}, {"LC_ALL", "C.UTF-8"}, {"COLORTERM", ""},
    {"PAGER", "cat"},        {"GIT_PAGER", "cat"},  {"GH_PAGER", "cat"},
};

#define EXEC_ENV_COUNT NC_ARRAY_LEN(EXEC_ENV)

static bool g_exec_env_active = true;

void exec_env_set_active(bool active) {
    g_exec_env_active = active;
}

bool exec_env_is_active(void) {
    return g_exec_env_active;
}

void exec_env_build_array(char **envp, int max, int *out_count) {
    int n = 0;
    if (!g_exec_env_active || !envp) {
        if (out_count)
            *out_count = 0;
        return;
    }

    for (int i = 0; i < EXEC_ENV_COUNT && n < max - 1; i++) {
        size_t klen = strlen(EXEC_ENV[i].key);
        size_t vlen = strlen(EXEC_ENV[i].value);
        char *entry = malloc(klen + 1 + vlen + 1);
        if (!entry)
            continue;
        snprintf(entry, klen + vlen + 2, "%s=%s", EXEC_ENV[i].key, EXEC_ENV[i].value);
        envp[n++] = entry;
    }
    envp[n] = NULL;
    if (out_count)
        *out_count = n;
}

void exec_env_free_array(char **envp, int count) {
    for (int i = 0; i < count; i++)
        free(envp[i]);
}

int exec_env_pair_count(void) {
    return EXEC_ENV_COUNT;
}

const char *exec_env_key_at(int idx) {
    if (idx < 0 || idx >= EXEC_ENV_COUNT)
        return NULL;
    return EXEC_ENV[idx].key;
}

const char *exec_env_value_at(int idx) {
    if (idx < 0 || idx >= EXEC_ENV_COUNT)
        return NULL;
    return EXEC_ENV[idx].value;
}
