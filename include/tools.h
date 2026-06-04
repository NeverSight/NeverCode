#ifndef TOOLS_H
#define TOOLS_H

#include "nevercode.h"

void tools_register(const tool_def_t *t);
void tools_register_all(void);
int tools_count(void);
const tool_def_t *tools_get_all(void);
const tool_def_t *tools_find(const char *name);
int tools_search(const char *query, const tool_def_t **results, int max);

/* Per-tool registration functions (called by tools_register_all) */
void tool_bash_register(void);
void tool_file_read_register(void);
void tool_file_write_register(void);
void tool_file_edit_register(void);
void tool_glob_register(void);
void tool_grep_register(void);
void tool_agent_register(void);
void tool_web_fetch_register(void);
void tool_web_search_register(void);
void tool_todo_register(void);
void tool_listdir_register(void);
void tool_notebook_edit_register(void);
void tool_enter_worktree_register(void);
void tool_exit_worktree_register(void);
void tool_tool_search_register(void);
void tool_ask_user_register(void);
void tool_apply_patch_register(void);
void tool_multi_agent_register(void);
void tool_config_register(void);
void tool_sleep_register(void);
void tool_suggest_register(void);
void tool_request_perms_register(void);
void tool_repl_register(void);
void tool_task_register(void);
void tool_mcp_resources_register(void);
void tool_git_diff_register(void);
void tool_plan_mode_register(void);
void tool_skill_register(void);
void tool_brief_register(void);
void tool_file_search_register(void);
void tool_image_view_register(void);
void tool_agent_control_register(void);
void tool_team_register(void);
void tool_structured_out_register(void);
void tool_mcp_auth_register(void);
void tool_cron_register(void);
void tool_scaffold_register(void);
void tool_multi_edit_register(void);
void tool_batch_register(void);
void tool_consensus_register(void);
void tool_workflow_register(void);
void tool_snip_register(void);
void tool_proc_monitor_register(void);

#endif /* TOOLS_H */
