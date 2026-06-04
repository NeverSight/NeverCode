/*
 * shell_completion.c — Generate shell auto-completion scripts
 *
 * Outputs zsh, bash, or fish completion definitions for the nevercode
 * binary so users get tab-completion for flags and slash commands.
 */

#include "nevercode.h"

static shell_type_t detect_shell(void) {
    const char *sh = nc_shell_env();
    if (!sh)
        return SHELL_UNKNOWN;
    if (strstr(sh, "zsh"))
        return SHELL_ZSH;
    if (strstr(sh, "bash"))
        return SHELL_BASH;
    if (strstr(sh, "fish"))
        return SHELL_FISH;
    return SHELL_UNKNOWN;
}

static shell_type_t parse_shell_arg(const char *name) {
    if (!name)
        return detect_shell();
    if (strcmp(name, "zsh") == 0)
        return SHELL_ZSH;
    if (strcmp(name, "bash") == 0)
        return SHELL_BASH;
    if (strcmp(name, "fish") == 0)
        return SHELL_FISH;
    return SHELL_UNKNOWN;
}

/* ── Flag table ── */

typedef struct {
    const char *short_flag;
    const char *long_flag;
    const char *desc;
} flag_entry_t;

static const flag_entry_t FLAGS[] = {
    {"-P", "--provider", "Provider: compat or native"},
    {"-b", "--base-url", "Custom API base URL"},
    {"-m", "--model", "Model name"},
    {"-k", "--key", "API key"},
    {"-p", "--prompt", "One-shot prompt"},
    {"-s", "--system-prompt", "Custom system prompt"},
    {"-A", "--append-system-prompt", "Append to system prompt"},
    {"-t", "--max-tokens", "Max output tokens"},
    {"-T", "--max-turns", "Max agent loop turns"},
    {"-e", "--effort", "Reasoning effort level"},
    {"-j", "--json", "JSON output mode"},
    {"-o", "--output", "Write result to file"},
    {"-S", "--schema", "JSON schema for structured output"},
    {"-r", "--resume", "Resume session by ID"},
    {"-c", "--continue", "Resume most recent session"},
    {"-F", "--profile", "Load config profile"},
    {"-v", "--verbose", "Verbose output"},
    {"-M", "--no-markdown", "Disable markdown rendering"},
    {"-a", "--approval", "Approval preset"},
    {"-D", "--dangerously-skip-permissions", "Skip permission prompts"},
    {"-d", "--cwd", "Set working directory"},
    {"-V", "--version", "Show version"},
    {"-h", "--help", "Show help"},
    {NULL, NULL, NULL}};

/* ── Generators ── */

static char *gen_zsh(void) {
    char *buf = malloc(NC_PATH_MAX);
    if (!buf)
        return NULL;
    int off = 0, cap = NC_PATH_MAX;

#define ZA(fmt, ...)                                                                               \
    do {                                                                                           \
        int w = snprintf(buf + off, cap - off, fmt, ##__VA_ARGS__);                                \
        off += w;                                                                                  \
    } while (0)

    ZA("#compdef nevercode\n\n");
    ZA("_nevercode() {\n");
    ZA("  local -a opts\n");
    ZA("  opts=(\n");
    for (const flag_entry_t *f = FLAGS; f->long_flag; f++) {
        if (f->short_flag)
            ZA("    '%s[%s]'\n", f->short_flag, f->desc);
        ZA("    '%s[%s]'\n", f->long_flag, f->desc);
    }
    ZA("  )\n");
    ZA("  _arguments -s $opts\n");
    ZA("}\n\n");
    ZA("_nevercode \"$@\"\n");
#undef ZA
    return buf;
}

static char *gen_bash(void) {
    char *buf = malloc(NC_PATH_MAX);
    if (!buf)
        return NULL;
    int off = 0, cap = NC_PATH_MAX;

#define BA(fmt, ...)                                                                               \
    do {                                                                                           \
        int w = snprintf(buf + off, cap - off, fmt, ##__VA_ARGS__);                                \
        off += w;                                                                                  \
    } while (0)

    BA("_nevercode_completions() {\n");
    BA("  local cur=\"${COMP_WORDS[COMP_CWORD]}\"\n");
    BA("  local opts=\"");
    for (const flag_entry_t *f = FLAGS; f->long_flag; f++) {
        if (f->short_flag)
            BA("%s ", f->short_flag);
        BA("%s ", f->long_flag);
    }
    BA("\"\n");
    BA("  COMPREPLY=( $(compgen -W \"$opts\" -- \"$cur\") )\n");
    BA("}\n\n");
    BA("complete -F _nevercode_completions nevercode\n");
#undef BA
    return buf;
}

static char *gen_fish(void) {
    char *buf = malloc(NC_PATH_MAX);
    if (!buf)
        return NULL;
    int off = 0, cap = NC_PATH_MAX;

#define FA(fmt, ...)                                                                               \
    do {                                                                                           \
        int w = snprintf(buf + off, cap - off, fmt, ##__VA_ARGS__);                                \
        off += w;                                                                                  \
    } while (0)

    for (const flag_entry_t *f = FLAGS; f->long_flag; f++) {
        const char *lf = f->long_flag + 2; /* strip -- */
        if (f->short_flag) {
            FA("complete -c nevercode -s %c -l %s -d '%s'\n", f->short_flag[1], lf, f->desc);
        } else {
            FA("complete -c nevercode -l %s -d '%s'\n", lf, f->desc);
        }
    }
#undef FA
    return buf;
}

/* ── Public API ── */

char *shell_completion_generate(const char *shell_name) {
    shell_type_t kind = parse_shell_arg(shell_name);
    switch (kind) {
    case SHELL_ZSH:
        return gen_zsh();
    case SHELL_BASH:
        return gen_bash();
    case SHELL_FISH:
        return gen_fish();
    default:
        return NULL;
    }
}

const char *shell_completion_detect(void) {
    shell_type_t k = detect_shell();
    switch (k) {
    case SHELL_ZSH:
        return "zsh";
    case SHELL_BASH:
        return "bash";
    case SHELL_FISH:
        return "fish";
    default:
        return "unknown";
    }
}

int shell_completion_install(const char *shell_name) {
    char *script = shell_completion_generate(shell_name);
    if (!script)
        return -1;

    shell_type_t kind = parse_shell_arg(shell_name);
    const char *ext = (kind == SHELL_ZSH) ? "zsh" : (kind == SHELL_FISH) ? "fish" : "bash";

    char dir[NC_PATH_MAX];
    config_home_path(dir, sizeof(dir), "");
    mkdir(dir, NC_DIR_MODE);

    char path[NC_PATH_MAX];
    snprintf(path, sizeof(path), "%s/completion.%s", dir, ext);

    int rc = nc_write_file_str(path, script);
    free(script);
    return rc;
}
