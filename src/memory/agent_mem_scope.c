/*
 * agent_mem_scope.c — Per-agent persistent memory with user/project/local scopes
 *
 * Each agent type gets an isolated memory directory. Scopes:
 *   USER:    ~/.agents/agent-memory/<type>/   (shared across projects)
 *   PROJECT: <cwd>/.agents/agent-memory/<type>/  (project-specific, VCS-friendly)
 *   LOCAL:   <cwd>/.agents/agent-memory-local/<type>/  (project-specific, gitignored)
 */

#include "nevercode.h"
#include "defaults.h"

static void sanitize_type(char *out, size_t sz, const char *agent_type) {
    size_t i = 0;
    for (const char *p = agent_type; *p && i < sz - 1; p++) {
        out[i++] = (*p == ':' || *p == '/') ? '-' : *p;
    }
    out[i] = '\0';
}

int agent_memory_dir(char *out, size_t sz, const char *agent_type, agent_mem_scope_t scope) {
    if (!out || !agent_type || !agent_type[0])
        return -1;

    char safe[NC_TAG_MAX];
    sanitize_type(safe, sizeof(safe), agent_type);

    switch (scope) {
    case AGENT_MEM_USER: {
        char base[NC_PATH_MAX];
        config_home_path(base, sizeof(base), AGENT_MEMORY_SUBDIR);
        snprintf(out, sz, "%s/%s", base, safe);
        break;
    }
    case AGENT_MEM_PROJECT: {
        char cwd[NC_PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd)))
            return -1;
        snprintf(out, sz, "%s/.agents/%s/%s", cwd, AGENT_MEMORY_SUBDIR, safe);
        break;
    }
    case AGENT_MEM_LOCAL: {
        char cwd[NC_PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd)))
            return -1;
        snprintf(out, sz, "%s/.agents/%s/%s", cwd, AGENT_MEMORY_LOCAL_SUBDIR, safe);
        break;
    }
    }
    return 0;
}

int agent_memory_ensure(const char *agent_type, agent_mem_scope_t scope) {
    char dir[NC_PATH_MAX];
    if (agent_memory_dir(dir, sizeof(dir), agent_type, scope) != 0)
        return -1;
    return nc_mkdir_p(dir);
}

char *agent_memory_prompt(const char *agent_type, agent_mem_scope_t scope, int max_tokens) {
    char dir[NC_PATH_MAX];
    if (agent_memory_dir(dir, sizeof(dir), agent_type, scope) != 0)
        return NULL;

    if (max_tokens <= 0)
        max_tokens = AGENT_MEMORY_MAX_PROMPT_TOKENS;

    struct stat st;
    if (stat(dir, &st) != 0)
        return NULL;

    memdir_header_t hdrs[MEMDIR_MAX_FILES];
    int n = memdir_scan(dir, hdrs, MEMDIR_MAX_FILES);
    if (n <= 0)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "<agent_memory scope=\"%s\" agent=\"%s\">\n",
                   scope == AGENT_MEM_USER      ? "user"
                   : scope == AGENT_MEM_PROJECT ? "project"
                                                : "local",
                   agent_type);

    int token_est = 0;
    for (int i = 0; i < n && token_est < max_tokens; i++) {
        if (!hdrs[i].filepath)
            continue;
        char *content = strbuf_read_file(hdrs[i].filepath, NULL);
        if (!content)
            continue;
        int ct = estimate_tokens(content);
        if (token_est + ct > max_tokens) {
            free(content);
            break;
        }
        const char *label = (hdrs[i].description && hdrs[i].description[0]) ? hdrs[i].description
                            : hdrs[i].filename                              ? hdrs[i].filename
                                                                            : "entry";
        strbuf_appendf(&sb, "## %s\n%s\n\n", label, content);
        token_est += ct;
        free(content);
    }

    strbuf_append_str(&sb, "</agent_memory>\n");
    memdir_free_headers(hdrs, n);
    return strbuf_detach(&sb);
}
