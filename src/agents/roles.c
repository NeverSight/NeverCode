/*
 * roles.c — Agent role system with config layering
 */
#include "nevercode.h"

/* AGENT_ROLE_DIR in defaults.h */
#define ROLE_DIR AGENT_ROLE_DIR

typedef struct {
    char *name;
    char *display_name;
    char *system_prompt;
    char *model;
    int max_turns;
    bool read_only;
    char *allowed_tools[MAX_AGENT_TOOLS];
    int num_allowed_tools;
    bool builtin;
} agent_role_t;

static agent_role_t g_roles[MAX_AGENT_ROLES];
static int g_role_count = 0;

static const struct {
    const char *name;
    const char *display;
    const char *prompt;
    bool read_only;
} builtin_roles[] = {
    {"default", "Default",
     "You are a capable coding assistant. Follow the user's instructions precisely.", false},
    {"explorer", "Explorer",
     "You are a file search specialist. You excel at thoroughly navigating and exploring "
     "codebases. "
     "This is a READ-ONLY exploration task. You are STRICTLY PROHIBITED from creating, modifying, "
     "or deleting files. "
     "Your strengths: "
     "- Rapidly finding files using glob patterns. "
     "- Searching code and text with powerful regex patterns. "
     "- Reading files to understand structure and logic. "
     "- Building a mental map of the codebase quickly. "
     "Strategy: Start broad with Glob/ListDir to understand structure, then narrow with Grep for "
     "specific symbols, "
     "then Read key files for deep understanding. Return concise, focused answers with file paths "
     "and line numbers.",
     true},
    {"awaiter", "Awaiter",
     "You are a background task awaiter. Monitor the specified process or condition and report "
     "back when it completes.",
     false},
    {"reviewer", "Code Reviewer",
     "You are a thorough code reviewer. Analyze code changes for bugs, security issues, "
     "performance problems, and style violations. Provide structured findings with severity "
     "levels. "
     "For each finding: [SEVERITY] file:line — description. "
     "Be constructive but rigorous. Focus on: correctness bugs, security flaws, resource leaks, "
     "race conditions, error handling gaps, and API contract violations. "
     "Skip cosmetic issues unless they impact readability significantly.",
     true},
    {"recon", "Reconnaissance",
     "You are a security reconnaissance specialist. Enumerate targets, identify services, discover "
     "vulnerabilities, and map attack surfaces.",
     false},
    {"exploit", "Exploit Developer",
     "You are an exploit development specialist. Analyze vulnerabilities, develop PoC exploits, "
     "test payloads, and document exploitation paths. Focus on reliability and stealth.",
     false},
    {"verifier", "Verification Specialist",
     "Your job is not to confirm the implementation works — it is to try to break it. "
     "You have two failure patterns to avoid: (1) verification avoidance — reading code and "
     "saying PASS without actually running anything; (2) being seduced by the first 80%% — "
     "the polished surface while half the buttons do nothing. "
     "Your entire value is in finding the last 20%%. "
     "DO NOT modify the project. You MAY write ephemeral test scripts to /tmp. "
     "REQUIRED BASELINE: (1) Read README/project docs for build/test commands. "
     "(2) Run the build — broken build is auto FAIL. "
     "(3) Run test suite — failing tests are auto FAIL. "
     "(4) Run linters/type-checkers if configured. "
     "Strategy by change type: "
     "Frontend: start dev server, curl endpoints AND subresources. "
     "Backend: curl endpoints, verify response shapes not just status codes, test error cases. "
     "CLI: run with representative AND malformed inputs (empty, boundary, unicode), verify exit "
     "codes. "
     "Bug fixes: reproduce original bug, verify fix, check for regressions. "
     "Infra/config: validate syntax, dry-run where possible, check env vars are referenced. "
     "Refactoring: existing tests MUST pass unchanged, diff the public API surface. "
     "ADVERSARIAL PROBES: Try concurrency (parallel mutating requests), "
     "boundary values (0, -1, empty, MAX_INT), idempotency (same request twice), "
     "orphan ops (reference non-existent IDs). "
     "RECOGNIZE YOUR RATIONALIZATIONS: "
     "'The code looks correct' — reading is not verification, run it. "
     "'Tests already pass' — verify independently. "
     "'This is probably fine' — probably is not verified, run it. "
     "For every check: state what you verify, the command run, output observed, "
     "and result (PASS/FAIL). End with VERDICT: PASS, FAIL, or PARTIAL.",
     true},
    {NULL, NULL, NULL, false}};

static void role_init(agent_role_t *r) {
    memset(r, 0, sizeof(*r));
}
static void role_free(agent_role_t *r) {
    if (!r)
        return;
    free(r->name);
    free(r->display_name);
    free(r->system_prompt);
    free(r->model);
    for (int i = 0; i < r->num_allowed_tools; i++)
        free(r->allowed_tools[i]);
    memset(r, 0, sizeof(*r));
}

void agent_roles_init(void) {
    g_role_count = 0;
    for (int i = 0; builtin_roles[i].name; i++) {
        if (g_role_count >= MAX_AGENT_ROLES)
            break;
        agent_role_t *r = &g_roles[g_role_count++];
        role_init(r);
        r->name = nc_xstrdup(builtin_roles[i].name);
        r->display_name = nc_xstrdup(builtin_roles[i].display);
        r->system_prompt = nc_xstrdup(builtin_roles[i].prompt);
        r->read_only = builtin_roles[i].read_only;
        r->builtin = true;
    }
}

void agent_roles_free(void) {
    for (int i = 0; i < g_role_count; i++)
        role_free(&g_roles[i]);
    g_role_count = 0;
}

static int parse_role_json(agent_role_t *r, const cJSON *root) {
    const char *name_str = nc_cjson_str(root, "name", NULL);
    if (!name_str)
        return -1;
    role_init(r);
    r->name = nc_xstrdup(name_str);
    r->display_name = nc_cjson_dup_str(root, "display_name", name_str);
    r->system_prompt = nc_cjson_dup_str(root, "system_prompt", NULL);
    r->model = nc_cjson_dup_str(root, "model", NULL);
    r->max_turns = nc_cjson_int(root, "max_turns", 0);
    r->read_only = nc_cjson_bool(root, "read_only", false);
    r->builtin = false;
    const cJSON *tools = cJSON_GetObjectItem(root, "allowed_tools");
    if (tools && cJSON_IsArray(tools)) {
        cJSON *item;
        cJSON_ArrayForEach(item, tools) {
            if (cJSON_IsString(item) && r->num_allowed_tools < MAX_AGENT_TOOLS)
                r->allowed_tools[r->num_allowed_tools++] = nc_xstrdup(item->valuestring);
        }
    }
    return 0;
}

int agent_roles_load_dir(const char *dir) {
    if (!dir)
        return 0;
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    int loaded = 0;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (!str_has_suffix(ent->d_name, ".json"))
            continue;
        char path[NC_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        size_t fsz = 0;
        char *buf = strbuf_read_file(path, &fsz);
        if (!buf)
            continue;
        cJSON *root = cJSON_Parse(buf);
        free(buf);
        if (!root)
            continue;
        if (g_role_count < MAX_AGENT_ROLES && parse_role_json(&g_roles[g_role_count], root) == 0) {
            g_role_count++;
            loaded++;
        }
        cJSON_Delete(root);
    }
    closedir(d);
    return loaded;
}

static const agent_role_t *role_find(const char *name) {
    if (!name)
        return NULL;
    for (int i = 0; i < g_role_count; i++)
        if (g_roles[i].name && strcmp(g_roles[i].name, name) == 0)
            return &g_roles[i];
    return NULL;
}

int agent_role_count(void) {
    return g_role_count;
}
const char *agent_role_name(int idx) {
    return (idx >= 0 && idx < g_role_count) ? g_roles[idx].name : NULL;
}
const char *agent_role_display(int idx) {
    return (idx >= 0 && idx < g_role_count) ? g_roles[idx].display_name : NULL;
}
const char *agent_role_prompt(const char *name) {
    const agent_role_t *r = role_find(name);
    return r ? r->system_prompt : NULL;
}
bool agent_role_is_readonly(const char *name) {
    const agent_role_t *r = role_find(name);
    return r ? r->read_only : false;
}

bool agent_role_tool_allowed(const char *role_name, const char *tool_name) {
    const agent_role_t *r = role_find(role_name);
    if (!r || r->num_allowed_tools == 0)
        return true;
    for (int i = 0; i < r->num_allowed_tools; i++)
        if (strcmp(r->allowed_tools[i], tool_name) == 0)
            return true;
    return false;
}

char *agent_roles_format_list(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Agent Roles (%d):\n", g_role_count);
    for (int i = 0; i < g_role_count; i++)
        strbuf_appendf(&sb, "  %-12s  %s%s\n", g_roles[i].name,
                       g_roles[i].display_name ? g_roles[i].display_name : "",
                       g_roles[i].builtin ? " [built-in]" : "");
    return strbuf_detach(&sb);
}

void agent_role_apply_to_spawn(const char *role_name, agent_spawn_opts_t *opts,
                               char **system_prompt_out, char **model_out) {
    const agent_role_t *r = role_find(role_name);
    if (!r)
        return;
    if (r->system_prompt && system_prompt_out)
        *system_prompt_out = nc_xstrdup(r->system_prompt);
    if (r->model && model_out)
        *model_out = nc_xstrdup(r->model);
    if (r->read_only && opts)
        opts->mode = FORK_LAST_N;
}
