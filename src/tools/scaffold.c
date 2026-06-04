/* scaffold.c — Scaffold tool: project environment setup for autonomous sessions.
 *
 * Wraps the init_agent subsystem as an LLM-callable tool so the model can
 * detect project type, generate setup scripts, and query init status on demand.
 */

#include "tools.h"
#include "harness.h"

static int scaffold_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;

    char cwd[NC_PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        *output = nc_xstrdup("error: no cwd");
        return 1;
    }

    const char *action = nc_cjson_str((cJSON *)input, "action", NULL);
    if (!action)
        action = "auto";

    strbuf_t sb;
    strbuf_init(&sb);

    if (strcmp(action, "detect") == 0) {
        int idx = init_agent_detect_type(cwd);
        if (idx >= 0) {
            strbuf_append_str(&sb, "Detected project type: ");
            strbuf_append_str(&sb, init_agent_type_name(idx));
            const char *run = init_agent_run_cmd(idx);
            const char *test = init_agent_test_cmd(idx);
            if (run) {
                strbuf_append_str(&sb, "\nBuild command: ");
                strbuf_append_str(&sb, run);
            }
            if (test) {
                strbuf_append_str(&sb, "\nTest command: ");
                strbuf_append_str(&sb, test);
            }
        } else {
            strbuf_append_str(&sb, "No known project type detected.");
        }

    } else if (strcmp(action, "init") == 0 || strcmp(action, "auto") == 0) {
        if (!init_agent_needed(cwd) && strcmp(action, "auto") == 0) {
            strbuf_append_str(&sb, "Project already initialized.");
        } else {
            int recipe = -1;
            int result = init_agent_run(cwd, &recipe);
            if (result > 0) {
                char *ctx_msg = init_agent_build_context(cwd, recipe);
                if (ctx_msg) {
                    strbuf_append_str(&sb, ctx_msg);
                    free(ctx_msg);
                }
                strbuf_append_str(&sb, "\nProject scaffolding complete.");
            } else {
                strbuf_append_str(&sb, "Scaffolding skipped.");
            }
        }

    } else if (strcmp(action, "script") == 0) {
        int idx = init_agent_detect_type(cwd);
        int rc = init_agent_gen_script(cwd, idx);
        if (rc == 0) {
            strbuf_append_str(&sb, "Generated init.sh");
            if (idx >= 0) {
                strbuf_append_str(&sb, " for ");
                strbuf_append_str(&sb, init_agent_type_name(idx));
                strbuf_append_str(&sb, " project");
            }
        } else {
            strbuf_append_str(&sb, "Failed to generate init.sh");
        }

    } else if (strcmp(action, "status") == 0) {
        bool needed = init_agent_needed(cwd);
        int idx = init_agent_detect_type(cwd);
        strbuf_append_str(&sb, needed ? "Status: not initialized\n" : "Status: initialized\n");
        if (idx >= 0) {
            strbuf_append_str(&sb, "Project type: ");
            strbuf_append_str(&sb, init_agent_type_name(idx));
            strbuf_append_str(&sb, "\n");
        }
        char buf[NC_MSG_MAX];
        snprintf(buf, sizeof(buf), "Recipes available: %d\n", init_agent_recipe_count());
        strbuf_append_str(&sb, buf);

    } else {
        strbuf_append_str(&sb, "Unknown action '");
        strbuf_append_str(&sb, action);
        strbuf_append_str(&sb, "'. Valid: auto, detect, init, script, status");
    }

    *output = strbuf_detach(&sb);
    return 0;
}

void tool_scaffold_register(void) {
    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");

    cJSON *props = cJSON_CreateObject();
    cJSON *act = cJSON_CreateObject();
    cJSON_AddStringToObject(act, "type", "string");
    cJSON_AddStringToObject(act, "description", "Action: auto, detect, init, script, status");

    cJSON *en = cJSON_CreateArray();
    cJSON_AddItemToArray(en, cJSON_CreateString("auto"));
    cJSON_AddItemToArray(en, cJSON_CreateString("detect"));
    cJSON_AddItemToArray(en, cJSON_CreateString("init"));
    cJSON_AddItemToArray(en, cJSON_CreateString("script"));
    cJSON_AddItemToArray(en, cJSON_CreateString("status"));
    cJSON_AddItemToObject(act, "enum", en);

    cJSON_AddItemToObject(props, "action", act);
    cJSON_AddItemToObject(schema, "properties", props);

    static tool_def_t def = {
        .name = "Scaffold",
        .description = "Set up project environment for long-running "
                       "autonomous sessions.",
        .execute = scaffold_exec,
    };
    def.input_schema = schema;
    tools_register(&def);
}
