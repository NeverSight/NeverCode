#include "mcp.h"
#include "tools.h"
#include "terminal.h"
#include <poll.h>

static cJSON *mcp_rpc(mcp_server_t *s, const char *method, cJSON *params, bool notify) {
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    if (!notify)
        cJSON_AddNumberToObject(req, "id", ++s->req_id);
    cJSON_AddStringToObject(req, "method", method);
    if (params)
        cJSON_AddItemToObject(req, "params", params);
    else
        cJSON_AddItemToObject(req, "params", cJSON_CreateObject());

    char *json = nc_cjson_detach(req);

    size_t len = strlen(json);
    char *tmp = realloc(json, len + 2);
    if (!tmp) {
        free(json);
        return NULL;
    }
    json = tmp;
    json[len] = '\n';
    json[len + 1] = '\0';

    write(s->stdin_fd, json, len + 1);
    free(json);

    if (notify)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    char buf[NC_IO_BUF];
    struct pollfd pfd = {.fd = s->stdout_fd, .events = POLLIN};

    while (1) {
        int r = poll(&pfd, 1, MCP_POLL_TIMEOUT_MS);
        if (r <= 0)
            break;
        ssize_t n = read(s->stdout_fd, buf, sizeof(buf));
        if (n <= 0)
            break;
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                if (sb.len > 0)
                    goto done;
                continue;
            }
            strbuf_append(&sb, &buf[i], 1);
        }
    }
done:;

    cJSON *resp = sb.data ? cJSON_Parse(sb.data) : NULL;
    strbuf_free(&sb);
    return resp;
}

int mcp_start(mcp_server_t *s, const char *name, const char *command) {
    memset(s, 0, sizeof(*s));
    s->name = nc_xstrdup(name);

    int in_pipe[2], out_pipe[2];
    if (nc_pipe_cloexec(in_pipe) < 0)
        return -1;
    if (nc_pipe_cloexec(out_pipe) < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        close(in_pipe[1]);
        close(out_pipe[0]);
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);
        close(out_pipe[1]);
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    s->pid = pid;
    s->stdin_fd = in_pipe[1];
    s->stdout_fd = out_pipe[0];

    cJSON *init_params = cJSON_CreateObject();
    cJSON_AddStringToObject(init_params, "protocolVersion", MCP_PROTOCOL_VERSION);
    cJSON *caps = cJSON_CreateObject();
    cJSON_AddItemToObject(init_params, "capabilities", caps);
    cJSON *client = cJSON_CreateObject();
    cJSON_AddStringToObject(client, "name", NC_MCP_CLIENT_NAME);
    cJSON_AddStringToObject(client, "version", NEVERCODE_VERSION);
    cJSON_AddItemToObject(init_params, "clientInfo", client);

    cJSON *resp = mcp_rpc(s, "initialize", init_params, false);
    if (!resp) {
        mcp_stop(s);
        return -1;
    }
    cJSON_Delete(resp);

    mcp_rpc(s, "notifications/initialized", NULL, true);
    return 0;
}

int mcp_discover_tools(mcp_server_t *s) {
    cJSON *resp = mcp_rpc(s, "tools/list", NULL, false);
    if (!resp)
        return -1;

    cJSON *result = cJSON_GetObjectItem(resp, "result");
    cJSON *tools = cJSON_GetObjectItem(result, "tools");
    int n = cJSON_GetArraySize(tools);

    s->tools = calloc(n, sizeof(tool_def_t));
    s->num_tools = 0;
    if (!s->tools)
        return -1;

    for (int i = 0; i < n; i++) {
        cJSON *t = cJSON_GetArrayItem(tools, i);
        const char *tname = nc_cjson_str(t, "name", NULL);
        const char *desc = nc_cjson_str(t, "description", NULL);
        cJSON *schema = cJSON_GetObjectItem(t, "inputSchema");
        if (!tname)
            continue;

        char full_name[NC_LABEL_MAX];
        snprintf(full_name, sizeof(full_name), "mcp_%s_%s", s->name, tname);

        s->tools[s->num_tools].name = nc_xstrdup(full_name);
        s->tools[s->num_tools].description = desc ? nc_xstrdup(desc) : nc_xstrdup("");
        s->tools[s->num_tools].input_schema = schema ? cJSON_Duplicate(schema, 1) : NULL;
        s->num_tools++;
    }

    cJSON_Delete(resp);
    return s->num_tools;
}

int mcp_call_tool(mcp_server_t *s, const char *tool_name, const cJSON *args, char **result) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", tool_name);
    if (args)
        cJSON_AddItemToObject(params, "arguments", cJSON_Duplicate(args, 1));

    cJSON *resp = mcp_rpc(s, "tools/call", params, false);
    if (!resp) {
        *result = nc_xstrdup("MCP call failed");
        return -1;
    }

    cJSON *r = cJSON_GetObjectItem(resp, "result");
    cJSON *content = cJSON_GetObjectItem(r, "content");
    if (content) {
        strbuf_t sb;
        strbuf_init(&sb);
        int nc = cJSON_GetArraySize(content);
        for (int i = 0; i < nc; i++) {
            cJSON *c = cJSON_GetArrayItem(content, i);
            const char *text = nc_cjson_str(c, "text", NULL);
            if (text)
                strbuf_append_str(&sb, text);
        }
        *result = strbuf_detach(&sb);
    } else {
        char *s2 = cJSON_Print(r ? r : resp);
        *result = s2;
    }

    bool is_err = cJSON_IsTrue(cJSON_GetObjectItem(r, "isError"));
    cJSON_Delete(resp);
    return is_err ? 1 : 0;
}

int mcp_list_resources(mcp_server_t *s, char **result) {
    cJSON *resp = mcp_rpc(s, "resources/list", NULL, false);
    if (!resp) {
        *result = nc_xstrdup("MCP resources/list failed");
        return -1;
    }

    cJSON *r = cJSON_GetObjectItem(resp, "result");
    cJSON *resources = cJSON_GetObjectItem(r, "resources");
    if (!resources || !cJSON_IsArray(resources)) {
        *result = nc_xstrdup("No resources available");
        cJSON_Delete(resp);
        return 0;
    }

    strbuf_t sb;
    strbuf_init(&sb);
    int nr = cJSON_GetArraySize(resources);
    for (int i = 0; i < nr; i++) {
        cJSON *res = cJSON_GetArrayItem(resources, i);
        const char *uri = nc_cjson_str(res, "uri", NULL);
        const char *name = nc_cjson_str(res, "name", NULL);
        const char *desc = nc_cjson_str(res, "description", NULL);
        const char *mime = nc_cjson_str(res, "mimeType", NULL);
        if (!uri)
            continue;

        if (sb.len > 0)
            strbuf_append_str(&sb, "\n");
        strbuf_append_str(&sb, "- ");
        if (name) {
            strbuf_append_str(&sb, name);
            strbuf_append_str(&sb, " (");
            strbuf_append_str(&sb, uri);
            strbuf_append_str(&sb, ")");
        } else {
            strbuf_append_str(&sb, uri);
        }
        if (mime) {
            strbuf_append_str(&sb, " [");
            strbuf_append_str(&sb, mime);
            strbuf_append_str(&sb, "]");
        }
        if (desc) {
            strbuf_append_str(&sb, "\n  ");
            strbuf_append_str(&sb, desc);
        }
    }

    *result = strbuf_detach(&sb);
    cJSON_Delete(resp);
    return nr;
}

int mcp_read_resource(mcp_server_t *s, const char *uri, char **result) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "uri", uri);

    cJSON *resp = mcp_rpc(s, "resources/read", params, false);
    if (!resp) {
        *result = nc_xstrdup("MCP resources/read failed");
        return -1;
    }

    cJSON *r = cJSON_GetObjectItem(resp, "result");
    cJSON *contents = cJSON_GetObjectItem(r, "contents");
    if (!contents || !cJSON_IsArray(contents)) {
        *result = cJSON_Print(r ? r : resp);
        cJSON_Delete(resp);
        return 0;
    }

    strbuf_t sb;
    strbuf_init(&sb);
    int nc = cJSON_GetArraySize(contents);
    for (int i = 0; i < nc; i++) {
        cJSON *c = cJSON_GetArrayItem(contents, i);
        const char *text = nc_cjson_str(c, "text", NULL);
        const char *blob = nc_cjson_str(c, "blob", NULL);
        if (text)
            strbuf_append_str(&sb, text);
        else if (blob) {
            strbuf_append_str(&sb, "[binary data: ");
            size_t blen = strlen(blob);
            char info[NC_FMT_MAX];
            snprintf(info, sizeof(info), "%zu bytes base64]", blen);
            strbuf_append_str(&sb, info);
        }
    }

    *result = strbuf_detach(&sb);
    cJSON_Delete(resp);
    return 0;
}

void mcp_stop(mcp_server_t *s) {
    if (s->stdin_fd > 0)
        close(s->stdin_fd);
    if (s->stdout_fd > 0)
        close(s->stdout_fd);
    if (s->pid > 0) {
        kill(s->pid, SIGTERM);
        for (int i = 0; i < 10; i++) {
            if (waitpid(s->pid, NULL, WNOHANG) != 0)
                goto reaped;
            usleep(PROC_POLL_FAST_US);
        }
        kill(s->pid, SIGKILL);
        waitpid(s->pid, NULL, 0);
    reaped:;
    }
    free(s->name);
    for (int i = 0; i < s->num_tools; i++) {
        free((char *)s->tools[i].name);
        free((char *)s->tools[i].description);
        if (s->tools[i].input_schema)
            cJSON_Delete(s->tools[i].input_schema);
    }
    free(s->tools);
    memset(s, 0, sizeof(*s));
}
