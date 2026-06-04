/*
 * shell_detect.c — Auto-detect the user's preferred shell
 *
 * Detects shell type from path/name and provides shell-specific
 * configuration (init files, prompt vars).
 */

#include "nevercode.h"

static shell_type_t g_detected_shell = SHELL_UNKNOWN;
static char g_shell_path[NC_MSG_MAX] = "";

shell_type_t shell_detect_from_path(const char *path) {
    if (!path || !*path)
        return SHELL_UNKNOWN;

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    if (strcmp(base, "zsh") == 0)
        return SHELL_ZSH;
    if (strcmp(base, "bash") == 0)
        return SHELL_BASH;
    if (strcmp(base, "sh") == 0)
        return SHELL_SH;
    if (strcmp(base, "fish") == 0)
        return SHELL_FISH;
    if (strcmp(base, "dash") == 0)
        return SHELL_DASH;
    if (strcmp(base, "ksh") == 0)
        return SHELL_KSH;
    if (strcmp(base, "csh") == 0)
        return SHELL_CSH;
    if (strcmp(base, "tcsh") == 0)
        return SHELL_TCSH;

    return SHELL_UNKNOWN;
}

const char *shell_type_name(shell_type_t type) {
    switch (type) {
    case SHELL_ZSH:
        return "zsh";
    case SHELL_BASH:
        return "bash";
    case SHELL_SH:
        return "sh";
    case SHELL_FISH:
        return "fish";
    case SHELL_DASH:
        return "dash";
    case SHELL_KSH:
        return "ksh";
    case SHELL_CSH:
        return "csh";
    case SHELL_TCSH:
        return "tcsh";
    default:
        return "unknown";
    }
}

const char *shell_init_file(shell_type_t type) {
    switch (type) {
    case SHELL_ZSH:
        return "~/.zshrc";
    case SHELL_BASH:
        return "~/.bashrc";
    case SHELL_FISH:
        return "~/.config/fish/config.fish";
    case SHELL_KSH:
        return "~/.kshrc";
    case SHELL_CSH:
    case SHELL_TCSH:
        return "~/.cshrc";
    default:
        return "~/.profile";
    }
}

const char *shell_prompt_var(shell_type_t type) {
    switch (type) {
    case SHELL_FISH:
        return "fish_prompt";
    case SHELL_CSH:
    case SHELL_TCSH:
        return "prompt";
    default:
        return "PS1";
    }
}

bool shell_supports_source(shell_type_t type) {
    return type != SHELL_CSH && type != SHELL_TCSH;
}

void shell_detect_init(void) {
    const char *shell_env = nc_shell_env();
    if (shell_env) {
        nc_strlcpy(g_shell_path, shell_env, sizeof(g_shell_path));
        g_detected_shell = shell_detect_from_path(shell_env);
        return;
    }

    const char *try_paths[] = {"/bin/zsh", "/bin/bash", "/bin/sh", NULL};
    for (int i = 0; try_paths[i]; i++) {
        if (access(try_paths[i], X_OK) == 0) {
            nc_strlcpy(g_shell_path, try_paths[i], sizeof(g_shell_path));
            g_detected_shell = shell_detect_from_path(try_paths[i]);
            return;
        }
    }

    g_detected_shell = SHELL_SH;
    snprintf(g_shell_path, sizeof(g_shell_path), "/bin/sh");
}

shell_type_t shell_detected(void) {
    return g_detected_shell;
}

const char *shell_detected_path(void) {
    return g_shell_path[0] ? g_shell_path : "/bin/sh";
}

char *shell_detect_describe(void) {
    char buf[NC_LINE_MAX];
    snprintf(buf, sizeof(buf),
             "Shell: %s (%s)\n"
             "Init file: %s\n"
             "Prompt var: %s",
             shell_type_name(g_detected_shell), shell_detected_path(),
             shell_init_file(g_detected_shell), shell_prompt_var(g_detected_shell));
    return nc_xstrdup(buf);
}
