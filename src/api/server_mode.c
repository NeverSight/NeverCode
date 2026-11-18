/*
 * server_mode.c — JSON-RPC server over localhost TCP.
 *
 * Binds a loopback-only socket for IDE/extension integration. Supports
 * multiple concurrent clients via non-blocking poll. Delegates RPC
 * dispatch to the connector module and adds built-in lifecycle methods:
 *   ping, status, shutdown, thread/start, thread/resume, thread/list
 */

#include "nevercode.h"
#include "config.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

typedef struct {
    int fd;
    char buf[SERVER_MODE_READ_BUF];
    int buf_len;
} srv_client_t;

typedef struct {
    int listen_fd;
    int port;
    bool running;
    int total_requests;
    int active_clients;
    int max_clients;
    long started_at;
    srv_client_t clients[SERVER_MODE_MAX_CLIENTS];
} server_ctx_t;

static server_ctx_t g_srv = {.listen_fd = -1};

/* ── Status Queries ── */

bool server_mode_active(void) {
    return g_srv.running && g_srv.listen_fd >= 0;
}

int server_mode_port(void) {
    return g_srv.port;
}

int server_mode_total_requests(void) {
    return g_srv.total_requests;
}

char *server_mode_format_status(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb,
                   "Server Mode:\n"
                   "  port: %d\n"
                   "  running: %s\n"
                   "  total requests: %d\n"
                   "  active clients: %d\n",
                   g_srv.port, g_srv.running ? "yes" : "no", g_srv.total_requests,
                   g_srv.active_clients);
    if (g_srv.started_at > 0)
        strbuf_appendf(&sb, "  uptime: %lds\n", nc_wall_sec() - g_srv.started_at);
    return strbuf_detach(&sb);
}

/* ── Socket Helpers ── */

static void close_client(int idx) {
    if (idx < 0 || idx >= SERVER_MODE_MAX_CLIENTS)
        return;
    srv_client_t *c = &g_srv.clients[idx];
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
        c->buf_len = 0;
        g_srv.active_clients--;
    }
}

static int accept_client(void) {
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    int fd = accept(g_srv.listen_fd, (struct sockaddr *)&addr, &alen);
    if (fd < 0)
        return -1;

    if (g_srv.active_clients >= g_srv.max_clients) {
        close(fd);
        return -1;
    }

    nc_set_nonblock(fd);

    for (int i = 0; i < SERVER_MODE_MAX_CLIENTS; i++) {
        if (g_srv.clients[i].fd < 0) {
            g_srv.clients[i].fd = fd;
            g_srv.clients[i].buf_len = 0;
            g_srv.active_clients++;
            return i;
        }
    }
    close(fd);
    return -1;
}

static void send_response(int client_idx, const char *resp) {
    if (client_idx < 0 || !resp)
        return;
    srv_client_t *c = &g_srv.clients[client_idx];
    if (c->fd < 0)
        return;

    size_t len = strlen(resp);
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(c->fd, resp + total, len - total);
        if (n <= 0)
            break;
        total += (size_t)n;
    }
    const char nl = '\n';
    if (write(c->fd, &nl, 1) < 0) {
        /* best effort: response already delivered on the wire */
    }
}

static void process_line(int client_idx, const char *line) {
    char *resp = NULL;
    connector_dispatch(line, &resp);
    g_srv.total_requests++;

    if (resp) {
        send_response(client_idx, resp);
        free(resp);
    }
}

static void process_client_data(int idx) {
    srv_client_t *c = &g_srv.clients[idx];
    if (c->fd < 0)
        return;

    int space = (int)sizeof(c->buf) - c->buf_len - 1;
    if (space <= 0) {
        c->buf_len = 0;
        return;
    }

    ssize_t n = read(c->fd, c->buf + c->buf_len, (size_t)space);
    if (n <= 0) {
        close_client(idx);
        return;
    }
    c->buf_len += (int)n;
    c->buf[c->buf_len] = '\0';

    char *start = c->buf;
    char *nl;
    while ((nl = strchr(start, '\n')) != NULL) {
        *nl = '\0';
        if (nl > start)
            process_line(idx, start);
        start = nl + 1;
    }

    int remaining = c->buf_len - (int)(start - c->buf);
    if (remaining > 0 && start != c->buf)
        memmove(c->buf, start, (size_t)remaining);
    c->buf_len = remaining;
}

/* ── Built-in RPC Handlers ── */

static int rpc_ping(const cJSON *params, cJSON **result, void *ctx) {
    (void)params;
    (void)ctx;
    *result = cJSON_CreateString("pong");
    return 0;
}

static int rpc_status(const cJSON *params, cJSON **result, void *ctx) {
    (void)params;
    (void)ctx;
    *result = cJSON_CreateObject();
    cJSON_AddStringToObject(*result, "version", NEVERCODE_VERSION);
    cJSON_AddNumberToObject(*result, "port", g_srv.port);
    cJSON_AddNumberToObject(*result, "clients", g_srv.active_clients);
    cJSON_AddNumberToObject(*result, "requests", g_srv.total_requests);
    cJSON_AddNumberToObject(*result, "uptime_s", (double)(nc_wall_sec() - g_srv.started_at));
    return 0;
}

static int rpc_shutdown(const cJSON *params, cJSON **result, void *ctx) {
    (void)params;
    (void)ctx;
    g_srv.running = false;
    *result = cJSON_CreateString("shutting_down");
    return 0;
}

static int rpc_thread_start(const cJSON *params, cJSON **result, void *ctx) {
    (void)ctx;
    const char *label = NULL;
    const char *model = NULL;
    if (params) {
        cJSON *lj = cJSON_GetObjectItem((cJSON *)params, "label");
        cJSON *mj = cJSON_GetObjectItem((cJSON *)params, "model");
        if (cJSON_IsString(lj))
            label = lj->valuestring;
        if (cJSON_IsString(mj))
            model = mj->valuestring;
    }

    thread_mgr_t *tm = thread_mgr_global();
    const char *resolved_model = model ? model : config_model(g_cfg_ptr);
    int idx = thread_mgr_create(tm, label ? label : "rpc-thread", NULL, resolved_model);
    if (idx < 0) {
        *result = cJSON_CreateString("error: max threads reached");
        return -1;
    }

    const thread_entry_t *te = &tm->threads[idx];
    *result = cJSON_CreateObject();
    cJSON_AddStringToObject(*result, "id", te->id);
    cJSON_AddStringToObject(*result, "label", te->label);
    cJSON_AddNumberToObject(*result, "index", idx);
    return 0;
}

static int rpc_thread_resume(const cJSON *params, cJSON **result, void *ctx) {
    (void)ctx;
    if (!params) {
        *result = cJSON_CreateString("error: missing thread id");
        return -1;
    }
    cJSON *ij = cJSON_GetObjectItem((cJSON *)params, "id");
    if (!cJSON_IsString(ij)) {
        *result = cJSON_CreateString("error: id must be string");
        return -1;
    }

    thread_mgr_t *tm = thread_mgr_global();
    int idx = thread_mgr_find(tm, ij->valuestring);
    if (idx < 0) {
        *result = cJSON_CreateString("error: thread not found");
        return -1;
    }

    int rc = thread_mgr_resume(tm, idx);
    *result = cJSON_CreateObject();
    cJSON_AddStringToObject(*result, "id", ij->valuestring);
    cJSON_AddBoolToObject(*result, "resumed", rc == 0);
    return rc;
}

static int rpc_thread_list(const cJSON *params, cJSON **result, void *ctx) {
    (void)params;
    (void)ctx;
    thread_mgr_t *tm = thread_mgr_global();

    *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(*result, "total", tm->count);
    cJSON_AddNumberToObject(*result, "active", thread_mgr_active_count(tm));

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < tm->count; i++) {
        const thread_entry_t *te = &tm->threads[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", te->id);
        cJSON_AddStringToObject(item, "label", te->label);
        cJSON_AddStringToObject(item, "state", thread_state_name(te->state));
        cJSON_AddNumberToObject(item, "turns", te->turn_count);
        cJSON_AddItemToArray(arr, item);
    }
    cJSON_AddItemToObject(*result, "threads", arr);
    return 0;
}

static int rpc_thread_complete(const cJSON *params, cJSON **result, void *ctx) {
    (void)ctx;
    if (!params) {
        *result = cJSON_CreateString("error: missing thread id");
        return -1;
    }
    cJSON *ij = cJSON_GetObjectItem((cJSON *)params, "id");
    if (!cJSON_IsString(ij)) {
        *result = cJSON_CreateString("error: id must be string");
        return -1;
    }
    thread_mgr_t *tm = thread_mgr_global();
    int idx = thread_mgr_find(tm, ij->valuestring);
    if (idx < 0) {
        *result = cJSON_CreateString("error: thread not found");
        return -1;
    }
    int rc = thread_mgr_complete(tm, idx);
    *result = cJSON_CreateObject();
    cJSON_AddStringToObject(*result, "id", ij->valuestring);
    cJSON_AddBoolToObject(*result, "completed", rc == 0);
    return rc;
}

static void register_builtin_methods(void) {
    connector_register("ping", rpc_ping, NULL);
    connector_register("status", rpc_status, NULL);
    connector_register("shutdown", rpc_shutdown, NULL);
    connector_register("thread/start", rpc_thread_start, NULL);
    connector_register("thread/resume", rpc_thread_resume, NULL);
    connector_register("thread/list", rpc_thread_list, NULL);
    connector_register("thread/complete", rpc_thread_complete, NULL);
}

/* ── Public API ── */

int server_mode_start(int port) {
    if (g_srv.running)
        return -1;

    if (port <= 0)
        port = SERVER_MODE_DEFAULT_PORT;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, SERVER_MODE_BACKLOG) < 0) {
        close(fd);
        return -1;
    }

    nc_set_nonblock(fd);

    g_srv.listen_fd = fd;
    g_srv.port = port;
    g_srv.running = true;
    g_srv.total_requests = 0;
    g_srv.active_clients = 0;
    g_srv.max_clients = SERVER_MODE_MAX_CLIENTS;
    g_srv.started_at = nc_wall_sec();

    for (int i = 0; i < SERVER_MODE_MAX_CLIENTS; i++)
        g_srv.clients[i].fd = -1;

    connector_init();
    register_builtin_methods();

    return 0;
}

void server_mode_stop(void) {
    if (!g_srv.running)
        return;
    g_srv.running = false;

    for (int i = 0; i < SERVER_MODE_MAX_CLIENTS; i++)
        close_client(i);

    if (g_srv.listen_fd >= 0) {
        close(g_srv.listen_fd);
        g_srv.listen_fd = -1;
    }
    connector_free();
}

int server_mode_poll(int timeout_ms) {
    if (!g_srv.running || g_srv.listen_fd < 0)
        return -1;

    int nfds = 1 + SERVER_MODE_MAX_CLIENTS;
    struct pollfd pfd[1 + SERVER_MODE_MAX_CLIENTS];
    memset(pfd, 0, sizeof pfd);

    pfd[0].fd = g_srv.listen_fd;
    pfd[0].events = POLLIN;

    for (int i = 0; i < SERVER_MODE_MAX_CLIENTS; i++) {
        pfd[1 + i].fd = g_srv.clients[i].fd;
        pfd[1 + i].events = (g_srv.clients[i].fd >= 0) ? POLLIN : 0;
    }

    int ready = poll(pfd, (nfds_t)nfds, timeout_ms);
    if (ready <= 0)
        return ready;

    if (pfd[0].revents & POLLIN)
        accept_client();

    for (int i = 0; i < SERVER_MODE_MAX_CLIENTS; i++) {
        if (pfd[1 + i].revents & (POLLIN | POLLHUP | POLLERR))
            process_client_data(i);
    }

    return ready;
}

void server_mode_broadcast(const char *json_notification) {
    if (!json_notification)
        return;
    for (int i = 0; i < SERVER_MODE_MAX_CLIENTS; i++) {
        if (g_srv.clients[i].fd >= 0)
            send_response(i, json_notification);
    }
}
