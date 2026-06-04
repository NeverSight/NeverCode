#ifndef MCP_H
#define MCP_H

#include "nevercode.h"

typedef struct {
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    int req_id;
    char *name;
    tool_def_t *tools;
    int num_tools;
} mcp_server_t;

int mcp_start(mcp_server_t *s, const char *name, const char *command);
int mcp_discover_tools(mcp_server_t *s);
int mcp_call_tool(mcp_server_t *s, const char *tool_name, const cJSON *args, char **result);
int mcp_list_resources(mcp_server_t *s, char **result);
int mcp_read_resource(mcp_server_t *s, const char *uri, char **result);
void mcp_stop(mcp_server_t *s);

#endif /* MCP_H */
