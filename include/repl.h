#ifndef REPL_H
#define REPL_H

#include "nevercode.h"
#include "config.h"
#include "conversation.h"
#include "permission.h"
#include "mcp.h"

/* MAX_MCP_SERVERS defined in defaults.h */

/* --- Global state (defined in main.c, used by split-out modules) --- */
extern perm_ctx_t g_perms;
extern config_t *g_cfg_ptr;
extern int g_compact_failures;
extern hook_ctx_t g_hooks;
extern bool g_plan_mode;
extern bool g_brief_mode;
extern char *g_compact_instructions;
extern memory_ctx_t g_memory;
extern time_t g_last_assistant_time;
extern read_file_tracker_t g_read_tracker;
extern int g_total_input_tokens;
extern int g_total_output_tokens;
extern bool g_warned_context;
extern mcp_server_t g_mcp_servers[MAX_MCP_SERVERS];
extern int g_num_mcp_servers;
extern volatile sig_atomic_t g_interrupted;

/* --- stream_handler.c --- */
typedef struct {
    strbuf_t text;
    struct {
        char id[NC_TAG_MAX];
        char name[NC_LABEL_MAX];
        strbuf_t args;
    } pending_tc[NEVERCODE_MAX_TOOL_CALLS];
    int num_tc;
    int stop_reason;
    bool had_error;
    char errmsg[NC_ERRBUF_MAX];
    int input_tokens;
    int output_tokens;
    int cache_creation_tokens;
    int cache_read_tokens;
    int turn_num;
    long request_start_ms;
    long ttft_ms; /* time-to-first-token (ms), -1 if not yet received */
} stream_state_t;

void stream_state_init(stream_state_t *s);
void stream_state_free(stream_state_t *s);
void on_stream_event(const stream_event_t *ev, void *ud);

/* --- context_manager.c --- */
typedef enum {
    CTX_ZONE_SAFE,
    CTX_ZONE_WARNING,
    CTX_ZONE_COMPACT,
    CTX_ZONE_BLOCKING
} context_zone_t;

int context_limit_for_model(const char *model);
context_zone_t get_context_zone(int est, int limit);
int auto_compact(conversation_t *conv);

/* --- context_compact.c (Level 3: Collapse) --- */
int context_collapse(conversation_t *conv, int keep_recent_rounds);
bool context_collapse_needed(int est_tokens, int limit);

typedef enum {
    API_ERR_UNKNOWN,
    API_ERR_CONTEXT_OVERFLOW,
    API_ERR_RATE_LIMIT,
    API_ERR_OVERLOADED,
    API_ERR_AUTH,
    API_ERR_NETWORK,
    API_ERR_INVALID_REQUEST,
    API_ERR_MODEL_NOT_FOUND,
    API_ERR_WATCHDOG
} api_error_class_t;

api_error_class_t classify_api_error(const char *msg);
const char *api_error_hint(api_error_class_t cls);

/* --- tool_executor.c --- */
void execute_pending_tools(conversation_t *conv, stream_state_t *st);
void auto_extract_memories(const conversation_t *conv, memory_ctx_t *mem);

/* --- slash_commands.c --- */
typedef enum { CMD_CONTINUE, CMD_EXIT, CMD_API_CALL, CMD_NOT_HANDLED } slash_result_t;

slash_result_t handle_slash_command(const char *input, conversation_t *conv, config_t *cfg,
                                    char **session_id, bool quiet_mode);

#endif /* REPL_H */
