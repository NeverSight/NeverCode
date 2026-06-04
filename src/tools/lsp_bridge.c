/*
 * lsp_bridge.c — Language Server Protocol JSON-RPC bridge
 *
 * Manages child LSP server processes via stdio. Supports the full set of
 * code-intelligence operations: go-to-definition, find-references, hover,
 * document-symbol, workspace-symbol, go-to-implementation, call-hierarchy
 * (prepare, incoming, outgoing).
 *
 * Protocol overview:
 *   1. Spawn an LSP server as a child process with stdin/stdout pipes.
 *   2. Send Content-Length framed JSON-RPC 2.0 messages.
 *   3. Read Content-Length framed responses, matching by request id.
 *
 * Thread safety: all state is module-global; call only from the main thread.
 */

#include "defaults.h"
#include "nevercode.h"

/* nc_lsp_json_int() uses case-insensitive lookup; LSP needs CaseSensitive */
static inline int lsp_json_int(const cJSON *obj, const char *key, int fb) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (item && cJSON_IsNumber(item)) ? item->valueint : fb;
}
#include <poll.h>

typedef struct {
    char *language;
    char *cmd;
    char *root_uri;
    lsp_state_t state;
    time_t started;
    pid_t pid;
    int in_fd;  /* write to server stdin */
    int out_fd; /* read from server stdout */
    int req_id;
    int caps_ready; /* non-zero after successful initialize handshake */
} lsp_conn_t;

static lsp_conn_t g_lsp[LSP_MAX_SERVERS];
static int g_lsp_n;

/* ── Lifecycle ── */

void lsp_bridge_init(void) {
    memset(g_lsp, 0, sizeof(g_lsp));
    g_lsp_n = 0;
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        g_lsp[i].in_fd = -1;
        g_lsp[i].out_fd = -1;
    }
}

static void lsp_close_conn(lsp_conn_t *c) {
    if (c->in_fd >= 0)
        close(c->in_fd);
    if (c->out_fd >= 0)
        close(c->out_fd);
    c->in_fd = -1;
    c->out_fd = -1;
    if (c->pid > 0) {
        kill(c->pid, SIGTERM);
        int st;
        waitpid(c->pid, &st, WNOHANG);
        c->pid = 0;
    }
    c->state = LSP_DISCONNECTED;
}

void lsp_bridge_free(void) {
    for (int i = 0; i < LSP_MAX_SERVERS; i++) {
        lsp_close_conn(&g_lsp[i]);
        free(g_lsp[i].language);
        free(g_lsp[i].cmd);
        free(g_lsp[i].root_uri);
    }
    memset(g_lsp, 0, sizeof(g_lsp));
    g_lsp_n = 0;
}

/* ── JSON-RPC framing ── */

static int lsp_send_raw(lsp_conn_t *c, const char *body, size_t len) {
    if (c->in_fd < 0)
        return -1;
    char hdr[NC_FMT_MAX];
    int hlen = snprintf(hdr, sizeof(hdr), "Content-Length: %zu\r\n\r\n", len);
    if (write(c->in_fd, hdr, hlen) != hlen)
        return -1;
    if (write(c->in_fd, body, len) != (ssize_t)len)
        return -1;
    return 0;
}

static char *lsp_recv_raw(lsp_conn_t *c, int timeout_ms) {
    if (c->out_fd < 0)
        return NULL;
    struct pollfd pfd = {.fd = c->out_fd, .events = POLLIN};

    /* Read header until \r\n\r\n */
    char hdr_buf[NC_LABEL_MAX];
    int hdr_len = 0;
    int content_len = -1;
    time_t deadline = nc_time_now() + (timeout_ms / 1000) + 1;

    while (content_len < 0) {
        if (nc_time_now() > deadline)
            return NULL;
        int pr =
            poll(&pfd, 1, timeout_ms > LSP_POLL_INTERVAL_MS ? LSP_POLL_INTERVAL_MS : timeout_ms);
        if (pr <= 0)
            return NULL;
        int n = read(c->out_fd, hdr_buf + hdr_len, 1);
        if (n <= 0)
            return NULL;
        hdr_len += n;
        hdr_buf[hdr_len] = '\0';
        char *sep = strstr(hdr_buf, "\r\n\r\n");
        if (sep) {
            *sep = '\0';
            const char *cl = strstr(hdr_buf, "Content-Length:");
            if (!cl)
                cl = strstr(hdr_buf, "content-length:");
            if (!cl)
                return NULL;
            content_len = nc_safe_atoi(cl + 15, 0);
            /* any bytes after \r\n\r\n are body preamble */
            int leftover = hdr_len - (int)(sep + 4 - hdr_buf);
            if (leftover > 0 && content_len > 0) {
                /* shift leftover bytes into body */
                char *body = calloc(1, content_len + 1);
                if (!body)
                    return NULL;
                memcpy(body, sep + 4, leftover);
                int remaining = content_len - leftover;
                int off = leftover;
                while (remaining > 0) {
                    if (nc_time_now() > deadline) {
                        free(body);
                        return NULL;
                    }
                    pr = poll(&pfd, 1, LSP_POLL_INTERVAL_MS);
                    if (pr <= 0)
                        continue;
                    n = read(c->out_fd, body + off, remaining);
                    if (n <= 0) {
                        free(body);
                        return NULL;
                    }
                    off += n;
                    remaining -= n;
                }
                body[content_len] = '\0';
                return body;
            }
            break;
        }
        if (hdr_len >= (int)sizeof(hdr_buf) - 2)
            return NULL;
    }

    if (content_len <= 0 || content_len > LSP_MAX_RESPONSE_SIZE)
        return NULL;

    char *body = calloc(1, content_len + 1);
    if (!body)
        return NULL;
    int off = 0;
    while (off < content_len) {
        if (nc_time_now() > deadline) {
            free(body);
            return NULL;
        }
        int pr = poll(&pfd, 1, LSP_POLL_INTERVAL_MS);
        if (pr <= 0)
            continue;
        int n = read(c->out_fd, body + off, content_len - off);
        if (n <= 0) {
            free(body);
            return NULL;
        }
        off += n;
    }
    body[content_len] = '\0';
    return body;
}

static cJSON *lsp_request(lsp_conn_t *c, const char *method, cJSON *params, int timeout_ms) {
    int id = ++c->req_id;
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", id);
    cJSON_AddStringToObject(req, "method", method);
    if (params)
        cJSON_AddItemToObject(req, "params", params);

    char *body = nc_cjson_detach(req);
    if (!body)
        return NULL;

    if (lsp_send_raw(c, body, strlen(body)) != 0) {
        free(body);
        return NULL;
    }
    free(body);

    /* Read responses until we get matching id (skip notifications) */
    time_t deadline = nc_time_now() + (timeout_ms / 1000) + 1;
    while (nc_time_now() <= deadline) {
        char *raw = lsp_recv_raw(c, timeout_ms);
        if (!raw)
            return NULL;
        cJSON *resp = cJSON_Parse(raw);
        free(raw);
        if (!resp)
            continue;
        cJSON *rid = cJSON_GetObjectItem(resp, "id");
        if (rid && rid->valueint == id)
            return resp;
        cJSON_Delete(resp);
    }
    return NULL;
}

static int lsp_notify(lsp_conn_t *c, const char *method, cJSON *params) {
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddStringToObject(req, "method", method);
    if (params)
        cJSON_AddItemToObject(req, "params", params);

    char *body = nc_cjson_detach(req);
    if (!body)
        return -1;

    int rc = lsp_send_raw(c, body, strlen(body));
    free(body);
    return rc;
}

/* ── Spawn & Initialize ── */

static int lsp_spawn(lsp_conn_t *c) {
    int to_child[2], from_child[2];
    if (nc_pipe_cloexec(to_child) != 0 || nc_pipe_cloexec(from_child) != 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        return -1;
    }
    if (pid == 0) {
        close(to_child[1]);
        close(from_child[0]);
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        close(to_child[0]);
        close(from_child[1]);
        execlp("/bin/sh", "sh", "-c", c->cmd, (char *)NULL);
        _exit(127);
    }

    close(to_child[0]);
    close(from_child[1]);
    c->in_fd = to_child[1];
    c->out_fd = from_child[0];
    c->pid = pid;

    nc_set_nonblock(c->out_fd);

    return 0;
}

static int lsp_initialize(lsp_conn_t *c) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "processId", (double)getpid());

    cJSON *caps = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "capabilities", caps);

    if (c->root_uri)
        cJSON_AddStringToObject(params, "rootUri", c->root_uri);

    cJSON *resp = lsp_request(c, "initialize", params, LSP_INIT_TIMEOUT_MS);
    if (!resp)
        return -1;

    cJSON *result = cJSON_GetObjectItem(resp, "result");
    if (!result) {
        cJSON_Delete(resp);
        return -1;
    }
    cJSON_Delete(resp);

    cJSON *note_params = cJSON_CreateObject();
    lsp_notify(c, "initialized", note_params);

    c->caps_ready = 1;
    c->state = LSP_READY;
    return 0;
}

/* ── Public API ── */

int lsp_bridge_start(const char *language, const char *command, const char *root_uri) {
    if (!language || !command || g_lsp_n >= LSP_MAX_SERVERS)
        return -1;

    int i = g_lsp_n;
    memset(&g_lsp[i], 0, sizeof(g_lsp[i]));
    g_lsp[i].in_fd = -1;
    g_lsp[i].out_fd = -1;
    g_lsp[i].language = nc_xstrdup(language);
    g_lsp[i].cmd = nc_xstrdup(command);
    g_lsp[i].root_uri = root_uri ? nc_xstrdup(root_uri) : NULL;
    if (!g_lsp[i].language || !g_lsp[i].cmd) {
        free(g_lsp[i].language);
        free(g_lsp[i].cmd);
        free(g_lsp[i].root_uri);
        memset(&g_lsp[i], 0, sizeof(g_lsp[i]));
        return -1;
    }

    g_lsp[i].state = LSP_STARTING;
    g_lsp[i].started = nc_time_now();

    if (lsp_spawn(&g_lsp[i]) != 0) {
        g_lsp[i].state = LSP_ERROR;
        g_lsp_n++;
        return -1;
    }

    if (lsp_initialize(&g_lsp[i]) != 0) {
        g_lsp[i].state = LSP_ERROR;
        g_lsp_n++;
        return i;
    }

    g_lsp_n++;
    return i;
}

int lsp_bridge_count(void) {
    return g_lsp_n;
}

const char *lsp_state_name(lsp_state_t s) {
    switch (s) {
    case LSP_DISCONNECTED:
        return "disconnected";
    case LSP_STARTING:
        return "starting";
    case LSP_READY:
        return "ready";
    case LSP_SHUTTING_DOWN:
        return "shutting_down";
    case LSP_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

char *lsp_bridge_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "lsp_bridge: %d connection(s)\n", g_lsp_n);
    for (int i = 0; i < g_lsp_n; i++) {
        strbuf_appendf(
            &sb, "  [%d] %s (%s) pid=%d state=%s\n", i, g_lsp[i].language ? g_lsp[i].language : "?",
            g_lsp[i].cmd ? g_lsp[i].cmd : "?", (int)g_lsp[i].pid, lsp_state_name(g_lsp[i].state));
    }
    return strbuf_detach(&sb);
}

/* ── Position-based helpers ── */

static cJSON *make_text_doc_id(const char *file_path) {
    cJSON *td = cJSON_CreateObject();
    strbuf_t uri;
    strbuf_init(&uri);
    if (file_path[0] == '/')
        strbuf_appendf(&uri, "file://%s", file_path);
    else
        strbuf_append_str(&uri, file_path);
    cJSON_AddStringToObject(td, "uri", uri.data);
    strbuf_free(&uri);
    return td;
}

static cJSON *make_position(int line, int character) {
    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", line);
    cJSON_AddNumberToObject(pos, "character", character);
    return pos;
}

static cJSON *make_text_doc_pos(const char *file_path, int line, int character) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", make_text_doc_id(file_path));
    cJSON_AddItemToObject(params, "position", make_position(line, character));
    return params;
}

/* ── Tool Operations ── */

static int lsp_find_conn(const char *language) {
    for (int i = 0; i < g_lsp_n; i++) {
        if (g_lsp[i].state == LSP_READY && g_lsp[i].language &&
            strcmp(g_lsp[i].language, language) == 0)
            return i;
    }
    return -1;
}

static char *format_location(cJSON *loc) {
    if (!loc)
        return nc_xstrdup("(no result)");
    strbuf_t sb;
    strbuf_init(&sb);

    if (cJSON_IsArray(loc)) {
        int n = cJSON_GetArraySize(loc);
        for (int i = 0; i < n && i < LSP_MAX_SERVERS; i++) {
            cJSON *item = cJSON_GetArrayItem(loc, i);
            cJSON *u = cJSON_GetObjectItem(item, "uri");
            cJSON *r = cJSON_GetObjectItem(item, "range");
            if (u && r) {
                cJSON *start = cJSON_GetObjectItem(r, "start");
                int line = start ? lsp_json_int(start, "line", -1) + 1 : 0;
                int col = start ? lsp_json_int(start, "character", -1) + 1 : 0;
                strbuf_appendf(&sb, "%s:%d:%d\n", u->valuestring, line, col);
            }
        }
    } else if (cJSON_IsObject(loc)) {
        cJSON *u = cJSON_GetObjectItem(loc, "uri");
        cJSON *r = cJSON_GetObjectItem(loc, "range");
        if (u && r) {
            cJSON *start = cJSON_GetObjectItem(r, "start");
            int line = start ? lsp_json_int(start, "line", -1) + 1 : 0;
            int col = start ? lsp_json_int(start, "character", -1) + 1 : 0;
            strbuf_appendf(&sb, "%s:%d:%d\n", u->valuestring, line, col);
        }
    }

    if (sb.len == 0) {
        strbuf_free(&sb);
        return nc_xstrdup("(no result)");
    }
    return strbuf_detach(&sb);
}

static char *format_hover(cJSON *result) {
    if (!result)
        return nc_xstrdup("(no hover information)");
    cJSON *contents = cJSON_GetObjectItem(result, "contents");
    if (!contents)
        return nc_xstrdup("(no hover information)");

    strbuf_t sb;
    strbuf_init(&sb);

    if (cJSON_IsString(contents)) {
        strbuf_append_str(&sb, contents->valuestring);
    } else if (cJSON_IsObject(contents)) {
        cJSON *val = cJSON_GetObjectItem(contents, "value");
        if (val && cJSON_IsString(val))
            strbuf_append_str(&sb, val->valuestring);
    } else if (cJSON_IsArray(contents)) {
        int n = cJSON_GetArraySize(contents);
        for (int i = 0; i < n; i++) {
            cJSON *item = cJSON_GetArrayItem(contents, i);
            if (cJSON_IsString(item))
                strbuf_appendf(&sb, "%s\n", item->valuestring);
            else if (cJSON_IsObject(item)) {
                cJSON *val = cJSON_GetObjectItem(item, "value");
                if (val && cJSON_IsString(val))
                    strbuf_appendf(&sb, "%s\n", val->valuestring);
            }
        }
    }

    if (sb.len == 0) {
        strbuf_free(&sb);
        return nc_xstrdup("(no hover information)");
    }
    return strbuf_detach(&sb);
}

static char *format_symbols(cJSON *result) {
    if (!result || !cJSON_IsArray(result))
        return nc_xstrdup("(no symbols)");
    strbuf_t sb;
    strbuf_init(&sb);
    int n = cJSON_GetArraySize(result);
    for (int i = 0; i < n; i++) {
        cJSON *sym = cJSON_GetArrayItem(result, i);
        cJSON *name = cJSON_GetObjectItem(sym, "name");
        cJSON *kind = cJSON_GetObjectItem(sym, "kind");
        cJSON *r = cJSON_GetObjectItem(sym, "range");
        if (!r)
            r = cJSON_GetObjectItem(sym, "location");
        int line = 0;
        if (r) {
            cJSON *s = cJSON_GetObjectItem(r, "start");
            if (!s) {
                cJSON *rng = cJSON_GetObjectItem(r, "range");
                if (rng)
                    s = cJSON_GetObjectItem(rng, "start");
            }
            if (s)
                line = lsp_json_int(s, "line", -1) + 1;
        }
        strbuf_appendf(&sb, "  %s (kind=%d) line %d\n", name ? name->valuestring : "?",
                       kind ? kind->valueint : 0, line);

        cJSON *children = cJSON_GetObjectItem(sym, "children");
        if (children && cJSON_IsArray(children)) {
            int cn = cJSON_GetArraySize(children);
            for (int j = 0; j < cn; j++) {
                cJSON *ch = cJSON_GetArrayItem(children, j);
                cJSON *cn2 = cJSON_GetObjectItem(ch, "name");
                cJSON *ck = cJSON_GetObjectItem(ch, "kind");
                cJSON *cr = cJSON_GetObjectItem(ch, "range");
                int cl = 0;
                if (cr) {
                    cJSON *cs = cJSON_GetObjectItem(cr, "start");
                    if (cs)
                        cl = lsp_json_int(cs, "line", -1) + 1;
                }
                strbuf_appendf(&sb, "    %s (kind=%d) line %d\n", cn2 ? cn2->valuestring : "?",
                               ck ? ck->valueint : 0, cl);
            }
        }
    }
    if (sb.len == 0) {
        strbuf_free(&sb);
        return nc_xstrdup("(no symbols)");
    }
    return strbuf_detach(&sb);
}

static char *format_call_hierarchy(cJSON *result) {
    if (!result || !cJSON_IsArray(result))
        return nc_xstrdup("(no call hierarchy)");
    strbuf_t sb;
    strbuf_init(&sb);
    int n = cJSON_GetArraySize(result);
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(result, i);
        cJSON *from = cJSON_GetObjectItem(item, "from");
        cJSON *to = cJSON_GetObjectItem(item, "to");
        cJSON *node = from ? from : (to ? to : item);
        cJSON *name = cJSON_GetObjectItem(node, "name");
        cJSON *uri = cJSON_GetObjectItem(node, "uri");
        cJSON *r = cJSON_GetObjectItem(node, "range");
        int line = 0;
        if (r) {
            cJSON *s = cJSON_GetObjectItem(r, "start");
            if (s)
                line = lsp_json_int(s, "line", -1) + 1;
        }
        strbuf_appendf(&sb, "  %s %s:%d\n", name ? name->valuestring : "?",
                       uri ? uri->valuestring : "?", line);
    }
    if (sb.len == 0) {
        strbuf_free(&sb);
        return nc_xstrdup("(no call hierarchy)");
    }
    return strbuf_detach(&sb);
}

/* ── Unified operation dispatch ── */

typedef enum {
    LSP_OP_DEFINITION,
    LSP_OP_REFERENCES,
    LSP_OP_HOVER,
    LSP_OP_DOC_SYMBOL,
    LSP_OP_WS_SYMBOL,
    LSP_OP_IMPLEMENTATION,
    LSP_OP_PREPARE_CALL,
    LSP_OP_INCOMING_CALLS,
    LSP_OP_OUTGOING_CALLS,
    LSP_OP_COUNT
} lsp_op_t;

static const char *lsp_op_method(lsp_op_t op) {
    switch (op) {
    case LSP_OP_DEFINITION:
        return "textDocument/definition";
    case LSP_OP_REFERENCES:
        return "textDocument/references";
    case LSP_OP_HOVER:
        return "textDocument/hover";
    case LSP_OP_DOC_SYMBOL:
        return "textDocument/documentSymbol";
    case LSP_OP_WS_SYMBOL:
        return "workspace/symbol";
    case LSP_OP_IMPLEMENTATION:
        return "textDocument/implementation";
    case LSP_OP_PREPARE_CALL:
        return "textDocument/prepareCallHierarchy";
    case LSP_OP_INCOMING_CALLS:
        return "callHierarchy/incomingCalls";
    case LSP_OP_OUTGOING_CALLS:
        return "callHierarchy/outgoingCalls";
    default:
        return NULL;
    }
}

static lsp_op_t lsp_parse_op(const char *s) {
    if (!s)
        return LSP_OP_COUNT;
    if (strcmp(s, "definition") == 0)
        return LSP_OP_DEFINITION;
    if (strcmp(s, "references") == 0)
        return LSP_OP_REFERENCES;
    if (strcmp(s, "hover") == 0)
        return LSP_OP_HOVER;
    if (strcmp(s, "documentSymbol") == 0)
        return LSP_OP_DOC_SYMBOL;
    if (strcmp(s, "workspaceSymbol") == 0)
        return LSP_OP_WS_SYMBOL;
    if (strcmp(s, "implementation") == 0)
        return LSP_OP_IMPLEMENTATION;
    if (strcmp(s, "prepareCallHierarchy") == 0)
        return LSP_OP_PREPARE_CALL;
    if (strcmp(s, "incomingCalls") == 0)
        return LSP_OP_INCOMING_CALLS;
    if (strcmp(s, "outgoingCalls") == 0)
        return LSP_OP_OUTGOING_CALLS;
    return LSP_OP_COUNT;
}

char *lsp_bridge_execute(const char *language, const char *operation, const char *file_path,
                         int line, int character, const char *query) {
    int idx = lsp_find_conn(language);
    if (idx < 0) {
        strbuf_t sb;
        strbuf_init(&sb);
        strbuf_appendf(&sb,
                       "No LSP server for language '%s'. "
                       "Start one with /lsp or lsp_bridge_start().",
                       language);
        return strbuf_detach(&sb);
    }

    lsp_conn_t *c = &g_lsp[idx];
    lsp_op_t op = lsp_parse_op(operation);
    if (op >= LSP_OP_COUNT)
        return nc_xstrdup("Unknown LSP operation");

    const char *method = lsp_op_method(op);
    cJSON *params = NULL;

    switch (op) {
    case LSP_OP_DEFINITION:
    case LSP_OP_REFERENCES:
    case LSP_OP_HOVER:
    case LSP_OP_IMPLEMENTATION:
    case LSP_OP_PREPARE_CALL:
        if (!file_path)
            return nc_xstrdup("filePath required");
        params = make_text_doc_pos(file_path, line > 0 ? line - 1 : 0,
                                   character > 0 ? character - 1 : 0);
        if (op == LSP_OP_REFERENCES) {
            cJSON *ctx = cJSON_CreateObject();
            cJSON_AddBoolToObject(ctx, "includeDeclaration", cJSON_True);
            cJSON_AddItemToObject(params, "context", ctx);
        }
        break;

    case LSP_OP_DOC_SYMBOL:
        if (!file_path)
            return nc_xstrdup("filePath required");
        params = cJSON_CreateObject();
        cJSON_AddItemToObject(params, "textDocument", make_text_doc_id(file_path));
        break;

    case LSP_OP_WS_SYMBOL:
        params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "query", query ? query : "");
        break;

    case LSP_OP_INCOMING_CALLS:
    case LSP_OP_OUTGOING_CALLS:
        if (!file_path)
            return nc_xstrdup("filePath required (prepare item)");
        params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "name", query ? query : "");
        cJSON_AddNumberToObject(params, "kind", 12);
        cJSON_AddItemToObject(params, "uri", cJSON_CreateString(file_path));
        cJSON *range = cJSON_CreateObject();
        cJSON_AddItemToObject(
            range, "start",
            make_position(line > 0 ? line - 1 : 0, character > 0 ? character - 1 : 0));
        cJSON_AddItemToObject(
            range, "end",
            make_position(line > 0 ? line - 1 : 0, character > 0 ? character - 1 : 0));
        cJSON_AddItemToObject(params, "range", range);
        break;

    default:
        return nc_xstrdup("Unhandled operation");
    }

    cJSON *resp = lsp_request(c, method, params, LSP_REQUEST_TIMEOUT_MS);
    if (!resp)
        return nc_xstrdup("LSP request timed out");

    cJSON *result = cJSON_GetObjectItem(resp, "result");
    cJSON *error = cJSON_GetObjectItem(resp, "error");
    char *out = NULL;

    if (error) {
        cJSON *msg = cJSON_GetObjectItem(error, "message");
        out = nc_xstrdup(msg && cJSON_IsString(msg) ? msg->valuestring : "LSP error");
    } else if (op == LSP_OP_HOVER) {
        out = format_hover(result);
    } else if (op == LSP_OP_DOC_SYMBOL || op == LSP_OP_WS_SYMBOL) {
        out = format_symbols(result);
    } else if (op == LSP_OP_INCOMING_CALLS || op == LSP_OP_OUTGOING_CALLS) {
        out = format_call_hierarchy(result);
    } else {
        out = format_location(result);
    }

    cJSON_Delete(resp);
    return out;
}

/* ── Open-document notification ── */

int lsp_bridge_did_open(const char *language, const char *file_path, const char *text,
                        const char *lang_id) {
    int idx = lsp_find_conn(language);
    if (idx < 0)
        return -1;

    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    strbuf_t uri;
    strbuf_init(&uri);
    if (file_path[0] == '/')
        strbuf_appendf(&uri, "file://%s", file_path);
    else
        strbuf_append_str(&uri, file_path);
    cJSON_AddStringToObject(td, "uri", uri.data);
    strbuf_free(&uri);
    cJSON_AddStringToObject(td, "languageId", lang_id ? lang_id : language);
    cJSON_AddNumberToObject(td, "version", 1);
    cJSON_AddStringToObject(td, "text", text ? text : "");
    cJSON_AddItemToObject(params, "textDocument", td);

    return lsp_notify(&g_lsp[idx], "textDocument/didOpen", params);
}

/* ── Shutdown ── */

int lsp_bridge_stop(int idx) {
    if (idx < 0 || idx >= g_lsp_n)
        return -1;
    lsp_conn_t *c = &g_lsp[idx];
    if (c->state != LSP_READY && c->state != LSP_STARTING)
        return -1;

    c->state = LSP_SHUTTING_DOWN;
    cJSON *resp = lsp_request(c, "shutdown", cJSON_CreateNull(), LSP_SHUTDOWN_TIMEOUT_MS);
    cJSON_Delete(resp);
    lsp_notify(c, "exit", cJSON_CreateNull());
    lsp_close_conn(c);
    return 0;
}
