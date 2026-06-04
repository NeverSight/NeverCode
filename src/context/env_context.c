/*
 * env_context.c — System environment context builder.
 *
 * Collects OS name, hostname, shell, user, cwd, and terminal info
 * at startup. Builds a tagged prompt section for the model. Supports
 * programmatic variable additions and staleness detection.
 */

#include "nevercode.h"
#include <sys/utsname.h>

typedef struct {
    char key[NC_FMT_MAX];
    char value[NC_LABEL_MAX];
    long set_time;
} env_var_t;

static struct {
    env_var_t vars[ENV_CTX_MAX_VARS];
    int count;
    long last_probe;
    bool initialized;
} g_env;

void env_ctx_init(void) {
    memset(&g_env, 0, sizeof(g_env));
    g_env.initialized = true;
}

void env_ctx_free(void) {
    memset(&g_env, 0, sizeof(g_env));
}

void env_ctx_set(const char *key, const char *value) {
    if (!g_env.initialized || !key)
        return;

    /* Update existing */
    for (int i = 0; i < g_env.count; i++) {
        if (strcmp(g_env.vars[i].key, key) == 0) {
            nc_strlcpy(g_env.vars[i].value, value ? value : "", sizeof(g_env.vars[i].value));
            g_env.vars[i].set_time = nc_wall_sec();
            return;
        }
    }

    /* Insert new */
    if (g_env.count < ENV_CTX_MAX_VARS) {
        nc_strlcpy(g_env.vars[g_env.count].key, key, sizeof(g_env.vars[g_env.count].key));
        nc_strlcpy(g_env.vars[g_env.count].value, value ? value : "",
                   sizeof(g_env.vars[g_env.count].value));
        g_env.vars[g_env.count].set_time = nc_wall_sec();
        g_env.count++;
    }
}

const char *env_ctx_get(const char *key) {
    if (!g_env.initialized || !key)
        return NULL;
    for (int i = 0; i < g_env.count; i++)
        if (strcmp(g_env.vars[i].key, key) == 0)
            return g_env.vars[i].value;
    return NULL;
}

int env_ctx_probe_system(void) {
    if (!g_env.initialized)
        return -1;

    struct utsname u;
    if (uname(&u) == 0) {
        env_ctx_set("os_name", u.sysname);
        env_ctx_set("os_release", u.release);
        env_ctx_set("machine", u.machine);
        env_ctx_set("hostname", u.nodename);
    }

    char cwd[NC_PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)))
        env_ctx_set("cwd", cwd);

    const char *shell = nc_shell_env();
    if (shell)
        env_ctx_set("shell", shell);

    const char *user = nc_user();
    if (user)
        env_ctx_set("user", user);

    const char *home = nc_home_dir();
    if (home)
        env_ctx_set("home", home);

    const char *term = nc_term_env();
    if (term)
        env_ctx_set("terminal", term);

    g_env.last_probe = nc_wall_sec();
    return g_env.count;
}

char *env_ctx_build_prompt_section(void) {
    if (!g_env.initialized || g_env.count == 0)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "<environment_context>\n");
    for (int i = 0; i < g_env.count; i++)
        strbuf_appendf(&sb, "%s: %s\n", g_env.vars[i].key, g_env.vars[i].value);
    strbuf_append_str(&sb, "</environment_context>\n");
    return strbuf_detach(&sb);
}

int env_ctx_count(void) {
    return g_env.count;
}

char *env_ctx_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Environment Context: %d vars\n", g_env.count);
    for (int i = 0; i < g_env.count; i++)
        strbuf_appendf(&sb, "  %s = %s\n", g_env.vars[i].key, g_env.vars[i].value);
    return strbuf_detach(&sb);
}
