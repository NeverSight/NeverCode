#include "nevercode.h"
#include "config.h"

char *agent_get_self_path(void) {
    static char path[NC_PATH_MAX];
    static int cached = 0;
    if (cached)
        return path;

#ifdef __APPLE__
    uint32_t sz = sizeof(path);
    extern int _NSGetExecutablePath(char *, uint32_t *);
    if (_NSGetExecutablePath(path, &sz) != 0)
        nc_strlcpy(path, NC_BINARY_NAME, sizeof(path));
#else
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0)
        nc_strlcpy(path, NC_BINARY_NAME, sizeof(path));
    else
        path[len] = '\0';
#endif

    cached = 1;
    return path;
}

int agent_build_exec_args(const void *cfg_ptr, const char *model, int max_turns, const char *prompt,
                          char *const **argv_out) {
    const config_t *cfg = (const config_t *)cfg_ptr;
    static const char *args[32];
    int n = 0;

    args[n++] = agent_get_self_path();

    if (cfg && cfg->provider == PROVIDER_COMPAT) {
        args[n++] = "--provider";
        args[n++] = "compat";
        const char *base = config_base_url(cfg);
        if (base) {
            args[n++] = "--base-url";
            args[n++] = base;
        }
    }

    const char *api_key = cfg ? config_api_key(cfg) : NULL;
    if (api_key) {
        args[n++] = "-k";
        args[n++] = api_key;
    }

    if (model) {
        args[n++] = "-m";
        args[n++] = model;
    }

    static char turns_str[NC_NUM_MAX];
    if (max_turns > 0) {
        snprintf(turns_str, sizeof(turns_str), "%d", max_turns);
        args[n++] = "--max-turns";
        args[n++] = turns_str;
    }

    args[n++] = "-p";
    args[n++] = prompt;
    args[n++] = "-D";
    args[n] = NULL;

    *argv_out = (char *const *)args;
    return n;
}
