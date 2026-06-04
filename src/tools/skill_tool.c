#include "tools.h"
#include "nevercode.h"

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"skill\":{\"type\":\"string\","
    "\"description\":\"The skill name (e.g. verify, debug, simplify, test, "
    "perf, security-review, batch, stuck, remember, loop, recon, exploit)\"},"
    "\"args\":{\"type\":\"string\","
    "\"description\":\"Optional arguments for the skill\"}"
    "},"
    "\"required\":[\"skill\"]"
    "}";

static int skill_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *skill_name = nc_cjson_str((cJSON *)input, "skill", NULL);
    if (!skill_name) {
        *output = nc_xstrdup("error: missing 'skill' parameter");
        return 1;
    }

    /* Strip leading slash for compatibility */
    if (skill_name[0] == '/')
        skill_name++;

    const char *prompt = skill_find_prompt(skill_name);
    if (!prompt) {
        strbuf_t sb;
        strbuf_init(&sb);
        char msg[NC_LABEL_MAX];
        snprintf(msg, sizeof(msg), "Unknown skill: '%s'\n\n", skill_name);
        strbuf_append_str(&sb, msg);

        char *list = NULL;
        skill_list_all(&list);
        if (list) {
            strbuf_append_str(&sb, list);
            free(list);
        }
        *output = strbuf_detach(&sb);
        return 1;
    }

    const char *args = nc_cjson_str((cJSON *)input, "args", NULL);

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, prompt);

    if (args && args[0]) {
        strbuf_append_str(&sb, "\n\nAdditional context: ");
        strbuf_append_str(&sb, args);
    }

    skill_usage_record(skill_name);

    *output = strbuf_detach(&sb);
    return 0;
}

static tool_def_t skill_tool;

void tool_skill_register(void) {
    skill_tool.name = "Skill";
    skill_tool.description = "Invoke a built-in skill to guide your approach. Skills provide "
                             "structured prompts for common tasks like verification, debugging, "
                             "code review, testing, and performance optimization.";
    skill_tool.input_schema = cJSON_Parse(SCHEMA);
    skill_tool.execute = skill_exec;
    skill_tool.read_only = true;
    skill_tool.concurrency_safe = true;
    skill_tool.destructive = false;
    tools_register(&skill_tool);
}
