#include "nevercode.h"
#include "defaults.h"
#include "tools.h"
#include "config.h"
void agent_def_init(agent_def_t *a) {
    memset(a, 0, sizeof(*a));
    a->max_turns = 30;
}

void agent_def_free(agent_def_t *a) {
    free(a->name);
    free(a->description);
    free(a->system_prompt);
    free(a->model);
    for (int i = 0; i < a->num_allowed_tools; i++)
        free(a->allowed_tools[i]);
    for (int i = 0; i < a->num_disallowed_tools; i++)
        free(a->disallowed_tools[i]);
    memset(a, 0, sizeof(*a));
}

static int parse_tool_list(const char *val, char **out, int max) {
    int count = 0;
    char buf[NC_PATH_MAX];
    nc_strlcpy(buf, val, sizeof(buf));

    char *p = buf;
    while (*p && count < max) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '-' || *p == ',')
            p++;
        if (*p == '\0')
            break;
        char *start = p;
        while (*p && *p != ',' && *p != '\n')
            p++;
        char save = *p;
        *p = '\0';
        char *trimmed = nc_trim(start);
        if (*trimmed) {
            out[count++] = nc_xstrdup(trimmed);
        }
        if (save)
            p++;
    }
    return count;
}

int agent_def_parse_md(agent_def_t *a, const char *content) {
    if (!content || !nc_starts_with(content, "---"))
        return -1;

    const char *fm_start = content + 3;
    while (*fm_start == '\n' || *fm_start == '\r')
        fm_start++;

    const char *fm_end = strstr(fm_start, "\n---");
    if (!fm_end)
        return -1;

    size_t fm_len = fm_end - fm_start;
    char *fm = malloc(fm_len + 1);
    if (!fm)
        return -1;
    memcpy(fm, fm_start, fm_len);
    fm[fm_len] = '\0';

    const char *body = fm_end + 4;
    while (*body == '\n' || *body == '\r' || *body == '-')
        body++;
    if (*body)
        a->system_prompt = nc_xstrdup(body);

    char *line = fm;
    char *next;
    char current_key[NC_LABEL_MAX] = {0};
    strbuf_t list_buf;
    strbuf_init(&list_buf);

    while (line && *line) {
        next = strchr(line, '\n');
        if (next)
            *next++ = '\0';

        if (line[0] == ' ' || line[0] == '\t') {
            if (current_key[0]) {
                strbuf_append_str(&list_buf, line);
                strbuf_append_str(&list_buf, "\n");
            }
            line = next;
            continue;
        }

        if (current_key[0] && list_buf.len > 0) {
            if (strcmp(current_key, "tools") == 0)
                a->num_allowed_tools =
                    parse_tool_list(list_buf.data, a->allowed_tools, MAX_AGENT_TOOLS);
            else if (strcmp(current_key, "disallowedTools") == 0)
                a->num_disallowed_tools =
                    parse_tool_list(list_buf.data, a->disallowed_tools, MAX_AGENT_TOOLS);
            strbuf_clear(&list_buf);
            current_key[0] = '\0';
        }

        char *colon = strchr(line, ':');
        if (!colon) {
            line = next;
            continue;
        }

        *colon = '\0';
        char *key = nc_trim(line);
        char *val = nc_trim(colon + 1);

        if (*val == '\0') {
            nc_strlcpy(current_key, key, sizeof(current_key));
            line = next;
            continue;
        }

        if (strcmp(key, "name") == 0) {
            free(a->name);
            a->name = nc_xstrdup(val);
        } else if (strcmp(key, "description") == 0) {
            free(a->description);
            a->description = nc_xstrdup(val);
        } else if (strcmp(key, "model") == 0) {
            free(a->model);
            a->model = nc_xstrdup(val);
        } else if (strcmp(key, "maxTurns") == 0) {
            a->max_turns = nc_safe_atoi(val, DEFAULT_MAX_TURNS);
            if (a->max_turns <= 0)
                a->max_turns = 30;
        } else if (strcmp(key, "background") == 0) {
            a->background = nc_parse_bool(val, false);
        } else if (strcmp(key, "effort") == 0) {
            if (strcmp(val, "low") == 0)
                a->effort = 1;
            else if (strcmp(val, "medium") == 0)
                a->effort = 2;
            else if (strcmp(val, "high") == 0)
                a->effort = 3;
        } else if (strcmp(key, "color") == 0) {
            if (strcmp(val, "red") == 0)
                a->color = 'r';
            else if (strcmp(val, "green") == 0)
                a->color = 'g';
            else if (strcmp(val, "blue") == 0)
                a->color = 'b';
            else if (strcmp(val, "yellow") == 0)
                a->color = 'y';
            else if (strcmp(val, "orange") == 0)
                a->color = 'o';
            else if (strcmp(val, "cyan") == 0)
                a->color = 'c';
        } else if (strcmp(key, "read_only") == 0 || strcmp(key, "readOnly") == 0) {
            a->read_only = nc_parse_bool(val, false);
        } else if (strcmp(key, "permissionMode") == 0) {
            if (strcmp(val, "acceptEdits") == 0)
                a->auto_accept = true;
        }

        line = next;
    }

    if (current_key[0] && list_buf.len > 0) {
        if (strcmp(current_key, "tools") == 0)
            a->num_allowed_tools =
                parse_tool_list(list_buf.data, a->allowed_tools, MAX_AGENT_TOOLS);
        else if (strcmp(current_key, "disallowedTools") == 0)
            a->num_disallowed_tools =
                parse_tool_list(list_buf.data, a->disallowed_tools, MAX_AGENT_TOOLS);
    }
    strbuf_free(&list_buf);
    free(fm);
    return 0;
}

bool agent_def_tool_allowed(const agent_def_t *a, const char *tool_name) {
    if (!a || !tool_name)
        return true;

    if (a->num_disallowed_tools > 0) {
        for (int i = 0; i < a->num_disallowed_tools; i++) {
            if (strcasecmp(a->disallowed_tools[i], tool_name) == 0)
                return false;
        }
    }

    if (a->num_allowed_tools > 0) {
        for (int i = 0; i < a->num_allowed_tools; i++) {
            if (strcmp(a->allowed_tools[i], "*") == 0)
                return true;
            if (strcasecmp(a->allowed_tools[i], tool_name) == 0)
                return true;
        }
        return false;
    }

    return true;
}

static agent_def_t g_builtin_agents[4];
static int g_num_builtin = 0;

static void register_builtin_agents(void) {
    if (g_num_builtin > 0)
        return;

    /* Explore Agent */
    agent_def_t *e = &g_builtin_agents[g_num_builtin++];
    agent_def_init(e);
    e->name = nc_xstrdup("explore");
    e->description = nc_xstrdup("Fast read-only code exploration agent");
    e->read_only = true;
    e->max_turns = 15;
    e->system_prompt =
        nc_xstrdup("You are a fast, read-only code exploration agent. Your job is to:\n"
                   "1. Search and read code to answer questions\n"
                   "2. Find file structures, function definitions, and dependencies\n"
                   "3. Provide accurate, concise answers\n\n"
                   "You CANNOT modify any files. You CANNOT run commands that change state.\n"
                   "Only use: Read, Glob, Grep, ListDir, WebFetch, WebSearch.");
    e->disallowed_tools[0] = nc_xstrdup("Agent");
    e->disallowed_tools[1] = nc_xstrdup("Write");
    e->disallowed_tools[2] = nc_xstrdup("Edit");
    e->disallowed_tools[3] = nc_xstrdup("NotebookEdit");
    e->num_disallowed_tools = 4;

    /* Plan Agent */
    agent_def_t *p = &g_builtin_agents[g_num_builtin++];
    agent_def_init(p);
    p->name = nc_xstrdup("plan");
    p->description = nc_xstrdup("Structured planning agent for implementation design");
    p->read_only = true;
    p->max_turns = NC_AGENT_PLAN_MAX_TURNS;
    p->system_prompt =
        nc_xstrdup("You are a structured planning agent. Your job is to:\n"
                   "1. Analyze code and requirements\n"
                   "2. Create detailed implementation plans with specific file paths\n"
                   "3. Identify risks and dependencies\n"
                   "4. End with a KEY FILES section listing files to modify\n\n"
                   "You CANNOT modify files. Focus on 'what to do', not 'how to style it'.\n"
                   "Output format: Problem Analysis → Steps → Key Files → Risks.");
    p->disallowed_tools[0] = nc_xstrdup("Agent");
    p->disallowed_tools[1] = nc_xstrdup("Write");
    p->disallowed_tools[2] = nc_xstrdup("Edit");
    p->disallowed_tools[3] = nc_xstrdup("NotebookEdit");
    p->num_disallowed_tools = 4;

    /* General Purpose Agent */
    agent_def_t *g = &g_builtin_agents[g_num_builtin++];
    agent_def_init(g);
    g->name = nc_xstrdup("general");
    g->description = nc_xstrdup("General-purpose agent with full tool access");
    g->max_turns = 30;
    g->system_prompt = nc_xstrdup("You are a general-purpose coding agent with full tool access.\n"
                                  "Complete the assigned task thoroughly and report back.");
    g->allowed_tools[0] = nc_xstrdup("*");
    g->num_allowed_tools = 1;
    g->disallowed_tools[0] = nc_xstrdup("Agent");
    g->num_disallowed_tools = 1;

    /* Verification Agent */
    agent_def_t *v = &g_builtin_agents[g_num_builtin++];
    agent_def_init(v);
    v->name = nc_xstrdup("verification");
    v->description = nc_xstrdup("Adversarial verification agent that tries to break code");
    v->max_turns = 25;
    v->background = true;
    v->color = 'r';
    v->system_prompt =
        nc_xstrdup("You are an ADVERSARIAL verification agent. Your goal is to BREAK the code.\n\n"
                   "DO NOT:\n"
                   "- Assume code works because it 'looks correct'\n"
                   "- Skip running tests because 'the logic seems fine'\n"
                   "- Accept surface-level correctness\n\n"
                   "DO:\n"
                   "- Run tests and check for failures\n"
                   "- Test edge cases and boundary conditions\n"
                   "- Check error handling paths\n"
                   "- Look for missing imports, type errors, race conditions\n"
                   "- Verify all files that should have been modified were modified\n\n"
                   "Report: list of issues found with severity (Critical/High/Medium/Low).");
    v->disallowed_tools[0] = nc_xstrdup("Agent");
    v->disallowed_tools[1] = nc_xstrdup("Write");
    v->disallowed_tools[2] = nc_xstrdup("Edit");
    v->disallowed_tools[3] = nc_xstrdup("NotebookEdit");
    v->num_disallowed_tools = 4;
}

const agent_def_t *agent_def_find(const char *name) {
    register_builtin_agents();
    for (int i = 0; i < g_num_builtin; i++) {
        if (strcasecmp(g_builtin_agents[i].name, name) == 0)
            return &g_builtin_agents[i];
    }
    return NULL;
}

int agent_def_list_builtin(const agent_def_t **out, int max) {
    register_builtin_agents();
    int n = NC_MIN(g_num_builtin, max);
    for (int i = 0; i < n; i++)
        out[i] = &g_builtin_agents[i];
    return n;
}
