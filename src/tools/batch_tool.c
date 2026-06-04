/*
 * batch_tool.c — Execute multiple tool calls in parallel within a single
 *                invocation.  Reduces model round-trips when several
 *                independent operations are needed.
 */

#include "tools.h"
#include "nevercode.h"
#include "terminal.h"

static const char *BATCH_DISALLOWED[] = {"Batch", "Agent", "SpawnAgent", "TeamCreate", NULL};

static bool batch_tool_disallowed(const char *name) {
    for (int i = 0; BATCH_DISALLOWED[i]; i++)
        if (strcasecmp(name, BATCH_DISALLOWED[i]) == 0)
            return true;
    return false;
}

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"tool_calls\":{\"type\":\"array\",\"items\":{\"type\":\"object\","
    "\"properties\":{"
    "\"tool\":{\"type\":\"string\",\"description\":\"Name of the tool to execute\"},"
    "\"parameters\":{\"type\":\"object\",\"description\":\"Parameters for the tool\"}"
    "},\"required\":[\"tool\",\"parameters\"]},"
    "\"description\":\"Array of tool calls to execute in parallel (max " NC_STRINGIFY(
        BATCH_MAX_CALLS) ")\"}"
                         "},"
                         "\"required\":[\"tool_calls\"]"
                         "}";

typedef struct {
    const tool_def_t *tool;
    cJSON *params;
    cJSON *synth_params;
    char *result;
    int rc;
    double elapsed_ms;
} batch_slot_t;

static void *batch_worker(void *arg) {
    batch_slot_t *slot = (batch_slot_t *)arg;
    long t0 = nc_mono_ms();
    slot->rc = slot->tool->execute(slot->params, &slot->result, NULL);
    slot->elapsed_ms = (double)(nc_mono_ms() - t0);
    return NULL;
}

static int batch_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    cJSON *calls = cJSON_GetObjectItem((cJSON *)input, "tool_calls");
    if (!calls || !cJSON_IsArray(calls) || cJSON_GetArraySize(calls) == 0) {
        *output = nc_xstrdup("error: tool_calls array is required and must not be empty");
        return 1;
    }

    int n = cJSON_GetArraySize(calls);
    if (n > BATCH_MAX_CALLS)
        n = BATCH_MAX_CALLS;

    batch_slot_t slots[BATCH_MAX_CALLS];
    memset(slots, 0, sizeof(slots));
    int valid = 0;

    strbuf_t sb;
    strbuf_init(&sb);

    for (int i = 0; i < n; i++) {
        cJSON *call = cJSON_GetArrayItem(calls, i);

        const char *name = nc_cjson_str(call, "tool", NULL);
        if (!name)
            name = nc_cjson_str(call, "name", NULL);
        cJSON *params = cJSON_GetObjectItem(call, "parameters");
        if (!params)
            params = cJSON_GetObjectItem(call, "arguments");

        /* Models often flatten params as siblings of "tool" instead of
         * nesting them under "parameters".  Collect remaining keys. */
        cJSON *synth_params = NULL;
        if (!params && call->child) {
            synth_params = cJSON_CreateObject();
            for (cJSON *kv = call->child; kv; kv = kv->next) {
                if (kv->string && strcmp(kv->string, "tool") != 0 &&
                    strcmp(kv->string, "name") != 0)
                    cJSON_AddItemToObject(synth_params, kv->string, cJSON_Duplicate(kv, 1));
            }
            if (cJSON_GetArraySize(synth_params) > 0)
                params = synth_params;
            else {
                cJSON_Delete(synth_params);
                synth_params = NULL;
            }
        }

        if (!name) {
            cJSON_Delete(synth_params);
            strbuf_appendf(&sb, "[%d] error: missing tool name\n", i + 1);
            continue;
        }
        if (batch_tool_disallowed(name)) {
            cJSON_Delete(synth_params);
            strbuf_appendf(&sb, "[%d] error: tool '%s' cannot be used in batch\n", i + 1, name);
            continue;
        }

        const tool_def_t *tool = tools_find(name);
        if (!tool) {
            cJSON_Delete(synth_params);
            strbuf_appendf(&sb, "[%d] error: tool '%s' not found\n", i + 1, name);
            continue;
        }

        slots[valid].tool = tool;
        slots[valid].params = params ? params : cJSON_CreateObject();
        slots[valid].synth_params = synth_params;
        valid++;
    }

    if (valid == 0) {
        strbuf_append_str(&sb, "error: no valid tool calls to execute");
        *output = strbuf_detach(&sb);
        return 1;
    }

    term_color(T_DIM, "(Batch: %d sub-tools)\n", valid);
    for (int i = 0; i < valid; i++) {
        const cJSON *p = slots[i].params;
        const char *hint = NULL;
        static const char *hint_keys[] = {"command", "path", "file_path", "pattern",
                                          "query",   "url",  "directory", NULL};
        if (p) {
            for (int k = 0; hint_keys[k] && !hint; k++)
                hint = nc_cjson_str(p, hint_keys[k], NULL);
        }
        if (hint)
            term_color(T_DIM, "  [%s] %s\n", slots[i].tool->name, hint);
        else
            term_color(T_DIM, "  [%s]\n", slots[i].tool->name);
    }

    pthread_t threads[BATCH_MAX_CALLS];
    bool threaded[BATCH_MAX_CALLS] = {false};

    long batch_t0 = nc_mono_ms();

    for (int i = 0; i < valid; i++) {
        if (slots[i].tool->concurrency_safe && i > 0) {
            if (pthread_create(&threads[i], NULL, batch_worker, &slots[i]) == 0) {
                threaded[i] = true;
                continue;
            }
        }
        batch_worker(&slots[i]);
    }

    for (int i = 0; i < valid; i++) {
        if (threaded[i])
            pthread_join(threads[i], NULL);
    }

    double batch_ms = (double)(nc_mono_ms() - batch_t0);

    int failures = 0;
    for (int i = 0; i < valid; i++) {
        strbuf_appendf(&sb, "[%d] %s (%.0fms): ", i + 1, slots[i].tool->name, slots[i].elapsed_ms);
        if (slots[i].rc != 0)
            failures++;

        if (slots[i].result) {
            size_t rlen = strlen(slots[i].result);
            if (rlen > BATCH_RESULT_TRUNC_MAX) {
                strbuf_append(&sb, slots[i].result, BATCH_RESULT_TRUNC_MAX);
                strbuf_appendf(&sb, "\n... (%zu bytes truncated)\n", rlen - BATCH_RESULT_TRUNC_MAX);
            } else {
                strbuf_append_str(&sb, slots[i].result);
                if (rlen > 0 && slots[i].result[rlen - 1] != '\n')
                    strbuf_append_str(&sb, "\n");
            }
            free(slots[i].result);
        } else {
            strbuf_append_str(&sb, "(no output)\n");
        }
        cJSON_Delete(slots[i].synth_params);
    }

    strbuf_appendf(&sb, "\n%d/%d calls completed successfully (%.0fms)", valid - failures, valid,
                   batch_ms);

    *output = strbuf_detach(&sb);
    /* Sub-tool failures are already detailed in the output text.  Returning 0
     * avoids setting is_error on the tool result which would trigger spurious
     * verify warnings for the meta-tool wrapper. */
    return 0;
}

static tool_def_t batch_tool;

void tool_batch_register(void) {
    memset(&batch_tool, 0, sizeof(batch_tool));
    batch_tool.name = "Batch";
    batch_tool.description =
        "Execute multiple tool calls in parallel within a single invocation. "
        "Each call specifies a tool name and its parameters. Use this when you "
        "need to run several independent operations (e.g. reading multiple files, "
        "running multiple greps) to reduce round-trips. Max 25 calls per batch. "
        "Agent/team tools cannot be nested inside a batch.";
    batch_tool.input_schema = cJSON_Parse(SCHEMA);
    batch_tool.execute = batch_exec;
    batch_tool.read_only = false;
    batch_tool.concurrency_safe = false;
    batch_tool.destructive = false;
    tools_register(&batch_tool);
}
