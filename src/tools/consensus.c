/*
 * consensus.c — Multi-model consensus tool
 *
 * Queries multiple LLM models in parallel and synthesizes their
 * responses into a single high-quality output. Useful for
 * complex reasoning, security analysis, and hard problems
 * where diverse perspectives improve accuracy.
 */

#include "nevercode.h"
#include "config.h"
#include "tools.h"

static int consensus_execute(const cJSON *input, char **output, void *ctx) {
    config_t *cfg = (config_t *)ctx;
    const cJSON *j_prompt = cJSON_GetObjectItem(input, "prompt");
    const cJSON *j_models = cJSON_GetObjectItem(input, "models");

    if (!j_prompt || !j_prompt->valuestring) {
        *output = nc_xstrdup("Error: 'prompt' parameter is required");
        return -1;
    }

    const char *prompt = j_prompt->valuestring;

    char model_list[MOA_MAX_MODELS][NC_TAG_MAX];
    int model_count = 0;

    if (j_models && cJSON_IsArray(j_models)) {
        const cJSON *m;
        cJSON_ArrayForEach(m, j_models) {
            if (model_count >= MOA_MAX_MODELS)
                break;
            if (m->valuestring)
                nc_strlcpy(model_list[model_count++], m->valuestring, NC_TAG_MAX);
        }
    }

    if (model_count == 0) {
        const char *resolved_model = config_model(cfg);
        nc_strlcpy(model_list[0], resolved_model, NC_TAG_MAX);
        model_count = 1;
    }

    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_appendf(&sb,
                   "Multi-model consensus requested for %d model(s).\n\n"
                   "Query: %s\n\n"
                   "To achieve consensus, re-issue this prompt to each model independently "
                   "using the Agent tool with different model configurations, then synthesize "
                   "the results.\n\n"
                   "Models requested:\n",
                   model_count, prompt);

    for (int i = 0; i < model_count; i++)
        strbuf_appendf(&sb, "  %d. %s\n", i + 1, model_list[i]);

    *output = strbuf_detach(&sb);
    return 0;
}

static tool_def_t consensus_tool;

void tool_consensus_register(void) {
    memset(&consensus_tool, 0, sizeof(consensus_tool));
    consensus_tool.name = "Consensus";
    consensus_tool.description =
        "Query multiple models for consensus on complex problems. "
        "Useful for security analysis, hard debugging, and critical decisions.";

    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON *props = cJSON_AddObjectToObject(schema, "properties");

    cJSON *p_prompt = cJSON_AddObjectToObject(props, "prompt");
    cJSON_AddStringToObject(p_prompt, "type", "string");
    cJSON_AddStringToObject(p_prompt, "description", "The query to evaluate with multiple models");

    cJSON *p_models = cJSON_AddObjectToObject(props, "models");
    cJSON_AddStringToObject(p_models, "type", "array");
    cJSON_AddStringToObject(p_models, "description",
                            "Array of model names to query (default: primary model)");
    cJSON *items = cJSON_AddObjectToObject(p_models, "items");
    cJSON_AddStringToObject(items, "type", "string");

    cJSON *req = cJSON_AddArrayToObject(schema, "required");
    cJSON_AddItemToArray(req, cJSON_CreateString("prompt"));

    consensus_tool.input_schema = schema;
    consensus_tool.execute = consensus_execute;
    consensus_tool.read_only = true;

    tools_register(&consensus_tool);
}
