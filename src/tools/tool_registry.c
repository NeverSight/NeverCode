#include "tools.h"

static tool_def_t g_tools[QUERY_MAX_TOOLS];
static int g_num_tools = 0;

void tools_register(const tool_def_t *t) {
    if (g_num_tools >= QUERY_MAX_TOOLS)
        return;
    for (int i = 0; i < g_num_tools; i++) {
        if (strcmp(g_tools[i].name, t->name) == 0) {
            g_tools[i] = *t;
            return;
        }
    }
    g_tools[g_num_tools++] = *t;
}

void tools_register_all(void) {
    tool_bash_register();
    tool_file_read_register();
    tool_file_write_register();
    tool_file_edit_register();
    tool_glob_register();
    tool_grep_register();
    tool_agent_register();
    tool_web_fetch_register();
    tool_web_search_register();
    tool_todo_register();
    tool_listdir_register();
    tool_notebook_edit_register();
    tool_enter_worktree_register();
    tool_exit_worktree_register();
    tool_tool_search_register();
    tool_ask_user_register();
    tool_apply_patch_register();
    tool_multi_agent_register();
    tool_config_register();
    tool_sleep_register();
    tool_suggest_register();
    tool_request_perms_register();
    tool_repl_register();
    tool_task_register();
    tool_mcp_resources_register();
    tool_git_diff_register();
    tool_plan_mode_register();
    tool_skill_register();
    tool_brief_register();
    tool_file_search_register();
    tool_image_view_register();
    tool_agent_control_register();
    tool_team_register();
    tool_structured_out_register();
    tool_mcp_auth_register();
    tool_cron_register();
    tool_scaffold_register();
    tool_multi_edit_register();
    tool_batch_register();
    tool_consensus_register();
    tool_workflow_register();
    tool_snip_register();
    tool_proc_monitor_register();
}

int tools_count(void) {
    return g_num_tools;
}

const tool_def_t *tools_get_all(void) {
    return g_num_tools > 0 ? g_tools : NULL;
}

const tool_def_t *tools_find(const char *name) {
    for (int i = 0; i < g_num_tools; i++)
        if (strcmp(g_tools[i].name, name) == 0)
            return &g_tools[i];
    return NULL;
}

int tools_search(const char *query, const tool_def_t **results, int max) {
    int found = 0;
    for (int i = 0; i < g_num_tools && found < max; i++) {
        if (nc_contains_ci(g_tools[i].name, query) || nc_contains_ci(g_tools[i].description, query))
            results[found++] = &g_tools[i];
    }
    return found;
}
