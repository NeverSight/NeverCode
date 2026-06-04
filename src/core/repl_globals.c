#include "repl.h"

/* Shared global state — used by main.c, stream_handler.c, tool_executor.c,
   context_manager.c, slash_commands.c */

perm_ctx_t g_perms;
int g_compact_failures = 0;
hook_ctx_t g_hooks;
bool g_plan_mode = false;
bool g_brief_mode = false;
char *g_compact_instructions = NULL;
memory_ctx_t g_memory;
time_t g_last_assistant_time = 0;
read_file_tracker_t g_read_tracker;
int g_total_input_tokens = 0;
int g_total_output_tokens = 0;
bool g_warned_context = false;
mcp_server_t g_mcp_servers[MAX_MCP_SERVERS];
int g_num_mcp_servers = 0;
volatile sig_atomic_t g_interrupted = 0;
