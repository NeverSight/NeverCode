#include "nevercode.h"
#include "tools.h"

/*
 * Tool Suggest — analyze user input and suggest appropriate tools.
 *
 * Provides proactive tool suggestions based on input patterns, helping
 * the model choose the right tool.
 */

typedef struct {
    const char *pattern;
    const char *tool;
    const char *reason;
} suggest_rule_t;

static const suggest_rule_t SUGGEST_RULES[] = {
    {"read file", "Read", "Use Read to view file contents"},
    {"show file", "Read", "Use Read to view file contents"},
    {"cat ", "Read", "Use Read to view file contents"},
    {"open file", "Read", "Use Read to view file contents"},
    {"view file", "Read", "Use Read to view file contents"},

    {"edit file", "Edit", "Use Edit for precise string replacement"},
    {"modify file", "Edit", "Use Edit for precise string replacement"},
    {"change file", "Edit", "Use Edit for precise string replacement"},
    {"replace ", "Edit", "Use Edit for string replacement"},
    {"fix the bug", "Edit", "Use Edit to fix code"},
    {"update file", "Edit", "Use Edit for precise modifications"},

    {"write file", "Write", "Use Write to create or overwrite files"},
    {"create file", "Write", "Use Write to create new files"},
    {"new file", "Write", "Use Write to create new files"},

    {"find file", "Glob", "Use Glob for file name pattern matching"},
    {"list file", "Glob", "Use Glob for file name pattern matching"},
    {"locate file", "Glob", "Use Glob to find files by pattern"},
    {"where is", "Glob", "Use Glob to locate files"},

    {"search for", "Grep", "Use Grep for content search with regex"},
    {"grep ", "Grep", "Use Grep for content search"},
    {"find in file", "Grep", "Use Grep to search file contents"},
    {"search code", "Grep", "Use Grep to search codebase"},

    {"run ", "Bash", "Use Bash to execute shell commands"},
    {"execute ", "Bash", "Use Bash to run commands"},
    {"install ", "Bash", "Use Bash for package installation"},
    {"build ", "Bash", "Use Bash for build commands"},
    {"test ", "Bash", "Use Bash to run tests"},
    {"compile ", "Bash", "Use Bash for compilation"},
    {"npm ", "Bash", "Use Bash for npm commands"},
    {"git ", "Bash", "Use Bash for git operations"},
    {"make ", "Bash", "Use Bash for make commands"},
    {"docker ", "Bash", "Use Bash for docker commands"},

    {"fetch url", "WebFetch", "Use WebFetch to retrieve web content"},
    {"download ", "WebFetch", "Use WebFetch to get web pages"},
    {"get url", "WebFetch", "Use WebFetch to retrieve URL content"},
    {"web page", "WebFetch", "Use WebFetch for webpage content"},

    {"search web", "WebSearch", "Use WebSearch for internet queries"},
    {"google ", "WebSearch", "Use WebSearch for web queries"},
    {"look up", "WebSearch", "Use WebSearch for information lookup"},

    {"list dir", "ListDir", "Use ListDir to list directory contents"},
    {"ls ", "ListDir", "Use ListDir to list directory"},

    {"apply patch", "ApplyPatch", "Use ApplyPatch for unified diff patches"},
    {"apply diff", "ApplyPatch", "Use ApplyPatch for unified diff patches"},
    {"patch file", "ApplyPatch", "Use ApplyPatch for unified diff patches"},

    {NULL, NULL, NULL}};

int tool_suggest_for_input(const char *input, tool_suggestion_t *out, int max) {
    if (!input || !out || max <= 0)
        return 0;

    char lower[NC_ERRBUF_MAX];
    size_t ilen = strlen(input);
    if (ilen > sizeof(lower) - 1)
        ilen = sizeof(lower) - 1;
    for (size_t i = 0; i < ilen; i++)
        lower[i] = tolower((unsigned char)input[i]);
    lower[ilen] = '\0';

    int count = 0;
    bool seen[64] = {false};

    for (int i = 0; SUGGEST_RULES[i].pattern && count < max; i++) {
        if (strstr(lower, SUGGEST_RULES[i].pattern)) {
            bool dup = false;
            for (int j = 0; j < count; j++) {
                if (strcmp(out[j].tool_name, SUGGEST_RULES[i].tool) == 0) {
                    dup = true;
                    break;
                }
            }
            if (dup)
                continue;
            (void)seen;

            out[count].tool_name = SUGGEST_RULES[i].tool;
            out[count].reason = SUGGEST_RULES[i].reason;
            out[count].confidence = 0.8;

            if (nc_starts_with(lower, SUGGEST_RULES[i].pattern))
                out[count].confidence = 0.95;

            count++;
        }
    }

    return count;
}

char *tool_suggest_format(const tool_suggestion_t *suggestions, int n) {
    if (!suggestions || n <= 0)
        return nc_xstrdup("(no tool suggestions)");

    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_str(&sb, "Suggested tools:\n");
    for (int i = 0; i < n; i++) {
        char line[NC_MSG_MAX];
        snprintf(line, sizeof(line), "  %d. %s — %s (%.0f%% confidence)\n", i + 1,
                 suggestions[i].tool_name, suggestions[i].reason, suggestions[i].confidence * 100);
        strbuf_append_str(&sb, line);
    }

    return strbuf_detach(&sb);
}

void tool_suggest_register(void) {
    static cJSON *schema = NULL;
    if (!schema) {
        schema = cJSON_CreateObject();
        cJSON_AddStringToObject(schema, "type", "object");
        cJSON *props = cJSON_AddObjectToObject(schema, "properties");
        cJSON *q = cJSON_AddObjectToObject(props, "query");
        cJSON_AddStringToObject(q, "type", "string");
        cJSON_AddStringToObject(q, "description",
                                "Natural language description of what you want to do");
        cJSON *req = cJSON_AddArrayToObject(schema, "required");
        cJSON_AddItemToArray(req, cJSON_CreateString("query"));
    }
    tool_def_t t = {.name = "ToolSuggest",
                    .description = "Analyze a task description and suggest appropriate tools",
                    .input_schema = schema,
                    .execute = tool_suggest_exec,
                    .read_only = true,
                    .concurrency_safe = true,
                    .destructive = false};
    tools_register(&t);
}

int tool_suggest_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *query = nc_cjson_str((cJSON *)input, "query", NULL);
    if (!query) {
        *output = nc_xstrdup("error: 'query' parameter required");
        return 1;
    }

    tool_suggestion_t suggestions[8];
    int n = tool_suggest_for_input(query, suggestions, 8);

    if (n == 0) {
        *output = nc_xstrdup("No specific tool suggestions for this query. "
                             "Use ToolSearch to find tools by keyword.");
        return 0;
    }

    *output = tool_suggest_format(suggestions, n);
    return 0;
}
