#include "nevercode.h"
#include "tools.h"
/*
 * Streaming Tool Executor — execute tools as they complete during streaming,
 * rather than waiting for all tool calls to be collected. Starts executing
 * tools as soon as their JSON arguments are fully received, overlapping
 * tool execution with continued model output.
 */

#define MAX_PENDING STREAMING_EXEC_MAX_PENDING

typedef struct {
    char id[NC_TAG_MAX];
    char name[NC_LABEL_MAX];
    strbuf_t args;
    bool args_complete;
    bool executed;
    int rc;
    char *output;
    pid_t bg_pid;
    int bg_pipe;
} streaming_tool_t;

struct streaming_exec {
    streaming_tool_t tools[MAX_PENDING];
    int count;
    int executed_count;
    bool enabled;
};

static streaming_exec_t g_sexec;

void streaming_exec_init(streaming_exec_t *se) {
    if (!se)
        return;
    memset(se, 0, sizeof(*se));
    se->enabled = true;
}

void streaming_exec_reset(streaming_exec_t *se) {
    if (!se)
        return;
    for (int i = 0; i < se->count; i++) {
        strbuf_free(&se->tools[i].args);
        free(se->tools[i].output);
    }
    memset(se, 0, sizeof(*se));
    se->enabled = true;
}

streaming_exec_t *streaming_exec_global(void) {
    return &g_sexec;
}

void streaming_exec_tool_begin(streaming_exec_t *se, const char *id, const char *name) {
    if (!se || se->count >= MAX_PENDING)
        return;
    int idx = se->count++;
    memset(&se->tools[idx], 0, sizeof(streaming_tool_t));
    if (id)
        nc_strlcpy(se->tools[idx].id, id, sizeof(se->tools[idx].id));
    if (name)
        nc_strlcpy(se->tools[idx].name, name, sizeof(se->tools[idx].name));
    strbuf_init(&se->tools[idx].args);
}

void streaming_exec_tool_delta(streaming_exec_t *se, const char *json_frag) {
    if (!se || se->count == 0 || !json_frag)
        return;
    streaming_tool_t *t = &se->tools[se->count - 1];
    if (!t->args_complete)
        strbuf_append_str(&t->args, json_frag);
}

void streaming_exec_tool_end(streaming_exec_t *se) {
    if (!se || se->count == 0)
        return;
    se->tools[se->count - 1].args_complete = true;
}

int streaming_exec_ready_count(const streaming_exec_t *se) {
    if (!se)
        return 0;
    int ready = 0;
    for (int i = 0; i < se->count; i++) {
        if (se->tools[i].args_complete && !se->tools[i].executed)
            ready++;
    }
    return ready;
}

int streaming_exec_total(const streaming_exec_t *se) {
    return se ? se->count : 0;
}

int streaming_exec_completed(const streaming_exec_t *se) {
    return se ? se->executed_count : 0;
}

bool streaming_exec_all_done(const streaming_exec_t *se) {
    if (!se)
        return true;
    for (int i = 0; i < se->count; i++) {
        if (!se->tools[i].executed)
            return false;
    }
    return true;
}

const char *streaming_exec_tool_id(const streaming_exec_t *se, int idx) {
    if (!se || idx < 0 || idx >= se->count)
        return NULL;
    return se->tools[idx].id;
}

const char *streaming_exec_tool_name(const streaming_exec_t *se, int idx) {
    if (!se || idx < 0 || idx >= se->count)
        return NULL;
    return se->tools[idx].name;
}

const char *streaming_exec_tool_args(const streaming_exec_t *se, int idx) {
    if (!se || idx < 0 || idx >= se->count)
        return NULL;
    return se->tools[idx].args.data;
}

const char *streaming_exec_tool_output(const streaming_exec_t *se, int idx) {
    if (!se || idx < 0 || idx >= se->count)
        return NULL;
    return se->tools[idx].output;
}

int streaming_exec_tool_rc(const streaming_exec_t *se, int idx) {
    if (!se || idx < 0 || idx >= se->count)
        return -1;
    return se->tools[idx].rc;
}

bool streaming_exec_tool_done(const streaming_exec_t *se, int idx) {
    if (!se || idx < 0 || idx >= se->count)
        return false;
    return se->tools[idx].executed;
}
