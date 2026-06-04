#include "nevercode.h"

/*
 * Shell snapshot — captures the shell environment (cwd, env vars, aliases)
 * between tool calls so that context is preserved and the model can reason
 * about the shell state.
 *
 * Takes a lightweight approach: capture cwd + selected env vars after each
 * bash tool invocation.
 */

/* SHELL_SNAPSHOT_SUBDIR in defaults.h */
#define SNAPSHOT_DIR SHELL_SNAPSHOT_SUBDIR
#define SNAPSHOT_MAX_AGE SHELL_SNAPSHOT_MAX_AGE
/* SHELL_SNAPSHOT_MAX_ENV in defaults.h */
#define MAX_ENV_CAPTURE SHELL_SNAPSHOT_MAX_ENV
/* NC_SSNAP_EXCLUDED_COUNT in defaults.h */

static const char *EXCLUDED_VARS[] = {"PWD", "OLDPWD", "_"};

typedef struct {
    char cwd[NC_PATH_MAX];
    char *env_keys[MAX_ENV_CAPTURE];
    char *env_vals[MAX_ENV_CAPTURE];
    int env_count;
    time_t timestamp;
} shell_snapshot_t;

static shell_snapshot_t g_snapshot;
static bool g_snapshot_valid = false;

static bool is_excluded(const char *var) {
    for (int i = 0; i < NC_SSNAP_EXCLUDED_COUNT; i++) {
        if (strcmp(EXCLUDED_VARS[i], var) == 0)
            return true;
    }
    return false;
}

void shell_snapshot_capture(void) {
    /* Free previous */
    for (int i = 0; i < g_snapshot.env_count; i++) {
        free(g_snapshot.env_keys[i]);
        free(g_snapshot.env_vals[i]);
    }
    memset(&g_snapshot, 0, sizeof(g_snapshot));

    if (!getcwd(g_snapshot.cwd, sizeof(g_snapshot.cwd)))
        nc_strlcpy(g_snapshot.cwd, ".", sizeof(g_snapshot.cwd));

    g_snapshot.timestamp = nc_time_now();

    extern char **environ;
    int count = 0;
    for (char **e = environ; *e && count < MAX_ENV_CAPTURE; e++) {
        char *eq = strchr(*e, '=');
        if (!eq)
            continue;

        size_t klen = (size_t)(eq - *e);
        char key[NC_LABEL_MAX];
        if (klen >= sizeof(key))
            continue;
        memcpy(key, *e, klen);
        key[klen] = '\0';

        if (is_excluded(key))
            continue;

        /* Only capture relevant env vars */
        if (nc_starts_with(key, NC_ENV_PREFIX) || nc_starts_with(key, "AGENT_") ||
            strcmp(key, "PATH") == 0 || strcmp(key, "HOME") == 0 || strcmp(key, "SHELL") == 0 ||
            strcmp(key, "TERM") == 0 || strcmp(key, "LANG") == 0 || strcmp(key, "EDITOR") == 0 ||
            strcmp(key, "VIRTUAL_ENV") == 0 || strcmp(key, "GOPATH") == 0 ||
            strcmp(key, "CARGO_HOME") == 0 || strcmp(key, "NODE_PATH") == 0) {
            g_snapshot.env_keys[count] = nc_xstrdup(key);
            g_snapshot.env_vals[count] = nc_xstrdup(eq + 1);
            count++;
        }
    }
    g_snapshot.env_count = count;
    g_snapshot_valid = true;
}

char *shell_snapshot_summary(void) {
    if (!g_snapshot_valid)
        return nc_xstrdup("(no shell snapshot)");

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Shell state:\n  cwd: %s\n", g_snapshot.cwd);

    if (g_snapshot.env_count > 0) {
        strbuf_append_str(&sb, "  env:\n");
        for (int i = 0; i < g_snapshot.env_count; i++) {
            char val_preview[NC_PREVIEW_MAX];
            nc_trunc_preview(val_preview, g_snapshot.env_vals[i], sizeof(val_preview));
            strbuf_appendf(&sb, "    %s=%s\n", g_snapshot.env_keys[i], val_preview);
        }
    }

    return strbuf_detach(&sb);
}

void shell_snapshot_free(void) {
    for (int i = 0; i < g_snapshot.env_count; i++) {
        free(g_snapshot.env_keys[i]);
        free(g_snapshot.env_vals[i]);
    }
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    g_snapshot_valid = false;
}
