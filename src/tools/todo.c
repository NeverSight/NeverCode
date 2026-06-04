/*
 * todo.c — Structured task list with dependency graph
 *
 * Supports blocks/blockedBy edges so the agent can express task ordering.
 * When a task is completed, any tasks it was blocking are checked: if all
 * their blockers are resolved, they become unblocked (status stays pending
 * but the "[blocked]" indicator drops).
 */

#include "tools.h"
#include "nevercode.h"

#define TODO_ICON_PENDING "[ ]"
#define TODO_ICON_ACTIVE "[>]"
#define TODO_ICON_DONE "[x]"
#define TODO_ICON_CANCEL "[-]"
#define TODO_ICON_BLOCKED "[B]"
/* TODO_MAX_DEPS defined in defaults.h */

static const char *SCHEMA =
    "{"
    "\"type\":\"object\","
    "\"properties\":{"
    "\"todos\":{\"type\":\"array\",\"description\":\"Array of todo items\","
    "\"items\":{\"type\":\"object\",\"properties\":{"
    "\"id\":{\"type\":\"string\"},"
    "\"content\":{\"type\":\"string\"},"
    "\"status\":{\"type\":\"string\",\"enum\":[\"pending\",\"in_progress\",\"completed\","
    "\"cancelled\"]},"
    "\"blocks\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},"
    "\"description\":\"IDs of tasks this task blocks\"},"
    "\"blockedBy\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},"
    "\"description\":\"IDs of tasks that block this task\"},"
    "\"owner\":{\"type\":\"string\",\"description\":\"Agent or user who owns this task\"},"
    "\"priority\":{\"type\":\"integer\",\"description\":\"Priority 1 (high) to 5 (low)\"}"
    "},\"required\":[\"id\",\"content\",\"status\"]}},"
    "\"merge\":{\"type\":\"boolean\",\"description\":\"If true, merge with existing todos by id\"}"
    "},"
    "\"required\":[\"todos\"]"
    "}";

static cJSON *g_todo_list;

static cJSON *find_todo_by_id(const char *id) {
    if (!g_todo_list || !id)
        return NULL;
    int n = cJSON_GetArraySize(g_todo_list);
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(g_todo_list, i);
        const char *eid = nc_cjson_str(item, "id", NULL);
        if (eid && strcmp(eid, id) == 0)
            return item;
    }
    return NULL;
}

static bool is_task_resolved(const char *id) {
    cJSON *t = find_todo_by_id(id);
    if (!t)
        return true;
    const char *s = nc_cjson_str(t, "status", NULL);
    return s && (strcmp(s, "completed") == 0 || strcmp(s, "cancelled") == 0);
}

static bool is_task_blocked(cJSON *item) {
    cJSON *blocked_by = cJSON_GetObjectItem(item, "blockedBy");
    if (!blocked_by || !cJSON_IsArray(blocked_by))
        return false;
    int n = cJSON_GetArraySize(blocked_by);
    for (int i = 0; i < n; i++) {
        cJSON *dep = cJSON_GetArrayItem(blocked_by, i);
        if (cJSON_IsString(dep) && !is_task_resolved(dep->valuestring))
            return true;
    }
    return false;
}

static void merge_string_array(cJSON *item, const char *key, cJSON *new_arr) {
    if (!new_arr || !cJSON_IsArray(new_arr) || cJSON_GetArraySize(new_arr) == 0)
        return;
    cJSON *existing = cJSON_GetObjectItem(item, key);
    if (!existing || !cJSON_IsArray(existing)) {
        cJSON_AddItemToObject(item, key, cJSON_Duplicate(new_arr, 1));
        return;
    }
    int n_new = cJSON_GetArraySize(new_arr);
    for (int i = 0; i < n_new && cJSON_GetArraySize(existing) < TODO_MAX_DEPS; i++) {
        cJSON *val = cJSON_GetArrayItem(new_arr, i);
        if (!cJSON_IsString(val))
            continue;
        bool dup = false;
        int n_ex = cJSON_GetArraySize(existing);
        for (int j = 0; j < n_ex; j++) {
            cJSON *e = cJSON_GetArrayItem(existing, j);
            if (cJSON_IsString(e) && strcmp(e->valuestring, val->valuestring) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup)
            cJSON_AddItemToArray(existing, cJSON_CreateString(val->valuestring));
    }
}

static void wire_bidirectional_deps(cJSON *item) {
    const char *id = nc_cjson_str(item, "id", NULL);
    if (!id)
        return;

    cJSON *blocks = cJSON_GetObjectItem(item, "blocks");
    if (blocks && cJSON_IsArray(blocks)) {
        int n = cJSON_GetArraySize(blocks);
        for (int i = 0; i < n; i++) {
            cJSON *target_id = cJSON_GetArrayItem(blocks, i);
            if (!cJSON_IsString(target_id))
                continue;
            cJSON *target = find_todo_by_id(target_id->valuestring);
            if (!target)
                continue;
            cJSON *arr = cJSON_CreateArray();
            cJSON_AddItemToArray(arr, cJSON_CreateString(id));
            merge_string_array(target, "blockedBy", arr);
            cJSON_Delete(arr);
        }
    }

    cJSON *blocked_by = cJSON_GetObjectItem(item, "blockedBy");
    if (blocked_by && cJSON_IsArray(blocked_by)) {
        int n = cJSON_GetArraySize(blocked_by);
        for (int i = 0; i < n; i++) {
            cJSON *dep_id = cJSON_GetArrayItem(blocked_by, i);
            if (!cJSON_IsString(dep_id))
                continue;
            cJSON *dep = find_todo_by_id(dep_id->valuestring);
            if (!dep)
                continue;
            cJSON *arr = cJSON_CreateArray();
            cJSON_AddItemToArray(arr, cJSON_CreateString(id));
            merge_string_array(dep, "blocks", arr);
            cJSON_Delete(arr);
        }
    }
}

static void merge_item_fields(cJSON *existing, cJSON *item) {
    const char *new_content = nc_cjson_str(item, "content", NULL);
    const char *new_status = nc_cjson_str(item, "status", NULL);
    const char *new_owner = nc_cjson_str(item, "owner", NULL);
    cJSON *new_priority = cJSON_GetObjectItem(item, "priority");

    if (new_content)
        cJSON_ReplaceItemInObject(existing, "content", cJSON_CreateString(new_content));
    if (new_status)
        cJSON_ReplaceItemInObject(existing, "status", cJSON_CreateString(new_status));
    if (new_owner) {
        if (cJSON_GetObjectItem(existing, "owner"))
            cJSON_ReplaceItemInObject(existing, "owner", cJSON_CreateString(new_owner));
        else
            cJSON_AddStringToObject(existing, "owner", new_owner);
    }
    if (new_priority && cJSON_IsNumber(new_priority)) {
        if (cJSON_GetObjectItem(existing, "priority"))
            cJSON_ReplaceItemInObject(existing, "priority",
                                      cJSON_CreateNumber(new_priority->valuedouble));
        else
            cJSON_AddNumberToObject(existing, "priority", new_priority->valuedouble);
    }

    merge_string_array(existing, "blocks", cJSON_GetObjectItem(item, "blocks"));
    merge_string_array(existing, "blockedBy", cJSON_GetObjectItem(item, "blockedBy"));
}

static const char *status_icon(const char *status, bool blocked) {
    if (blocked)
        return TODO_ICON_BLOCKED;
    if (!status)
        return TODO_ICON_PENDING;
    if (strcmp(status, "completed") == 0)
        return TODO_ICON_DONE;
    if (strcmp(status, "in_progress") == 0)
        return TODO_ICON_ACTIVE;
    if (strcmp(status, "cancelled") == 0)
        return TODO_ICON_CANCEL;
    return TODO_ICON_PENDING;
}

static int todo_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    cJSON *todos_in = cJSON_GetObjectItem((cJSON *)input, "todos");
    if (!todos_in || !cJSON_IsArray(todos_in)) {
        *output = nc_xstrdup("error: missing or invalid 'todos' array");
        return 1;
    }

    cJSON *merge_j = cJSON_GetObjectItem((cJSON *)input, "merge");
    bool merge = merge_j && cJSON_IsTrue(merge_j);

    if (!merge || !g_todo_list) {
        if (g_todo_list)
            cJSON_Delete(g_todo_list);
        g_todo_list = cJSON_Duplicate(todos_in, 1);
    } else {
        int n_in = cJSON_GetArraySize(todos_in);
        for (int i = 0; i < n_in; i++) {
            cJSON *item = cJSON_GetArrayItem(todos_in, i);
            const char *id = nc_cjson_str(item, "id", NULL);
            if (!id)
                continue;

            cJSON *existing = find_todo_by_id(id);
            if (existing)
                merge_item_fields(existing, item);
            else
                cJSON_AddItemToArray(g_todo_list, cJSON_Duplicate(item, 1));
        }
    }

    int n = cJSON_GetArraySize(g_todo_list);
    for (int i = 0; i < n; i++)
        wire_bidirectional_deps(cJSON_GetArrayItem(g_todo_list, i));

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "Todo list updated:\n");

    int done = 0, active = 0, blocked = 0, pending = 0;
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(g_todo_list, i);
        const char *id = nc_cjson_str(item, "id", NULL);
        const char *content = nc_cjson_str(item, "content", NULL);
        const char *status = nc_cjson_str(item, "status", NULL);
        const char *owner = nc_cjson_str(item, "owner", NULL);
        bool is_blocked = is_task_blocked(item);

        const char *icon = status_icon(status, is_blocked);

        if (status && strcmp(status, "completed") == 0)
            done++;
        else if (status && strcmp(status, "cancelled") == 0)
            done++;
        else if (is_blocked)
            blocked++;
        else if (status && strcmp(status, "in_progress") == 0)
            active++;
        else
            pending++;

        char line[NC_CMD_MAX];
        if (owner)
            snprintf(line, sizeof(line), "  %s %s: %s (@%s)\n", icon, id ? id : "?",
                     content ? content : "", owner);
        else
            snprintf(line, sizeof(line), "  %s %s: %s\n", icon, id ? id : "?",
                     content ? content : "");
        strbuf_append_str(&sb, line);

        cJSON *deps = cJSON_GetObjectItem(item, "blockedBy");
        if (deps && cJSON_IsArray(deps) && cJSON_GetArraySize(deps) > 0) {
            strbuf_append_str(&sb, "        blocked by: ");
            int nd = cJSON_GetArraySize(deps);
            for (int j = 0; j < nd; j++) {
                cJSON *d = cJSON_GetArrayItem(deps, j);
                if (j > 0)
                    strbuf_append_str(&sb, ", ");
                strbuf_append_str(&sb, cJSON_IsString(d) ? d->valuestring : "?");
            }
            strbuf_append_str(&sb, "\n");
        }
    }

    char summary[NC_LABEL_MAX];
    snprintf(summary, sizeof(summary), "  --- %d done, %d active, %d pending, %d blocked\n", done,
             active, pending, blocked);
    strbuf_append_str(&sb, summary);

    *output = strbuf_detach(&sb);
    return 0;
}

static tool_def_t todo_tool;

void tool_todo_register(void) {
    todo_tool.name = "TodoWrite";
    todo_tool.description = "Create and manage a structured task list with dependency tracking. "
                            "Use blocks/blockedBy arrays for task ordering. "
                            "Use merge=true to update existing items by id.";
    todo_tool.input_schema = cJSON_Parse(SCHEMA);
    todo_tool.execute = todo_exec;
    todo_tool.read_only = false;
    todo_tool.concurrency_safe = false;
    todo_tool.destructive = false;
    tools_register(&todo_tool);
}
