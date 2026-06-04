#include "test.h"
#include "nevercode.h"
#include "config.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static int pick_port(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return 18000;
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    bind(fd, (struct sockaddr *)&a, sizeof(a));
    socklen_t len = sizeof(a);
    getsockname(fd, (struct sockaddr *)&a, &len);
    int port = ntohs(a.sin_port);
    close(fd);
    return port;
}

static int test_not_active(void) {
    ASSERT_FALSE(server_mode_active());
    return 0;
}

static int test_format(void) {
    char *s = server_mode_format_status();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Server Mode");
    free(s);
    return 0;
}

static int test_total_requests(void) {
    ASSERT_INT_EQ(0, server_mode_total_requests());
    return 0;
}

static int test_port(void) {
    int p = server_mode_port();
    ASSERT_GE(p, 0);
    return 0;
}

static int test_start_stop(void) {
    int port = pick_port();
    int rc = server_mode_start(port);
    ASSERT_INT_EQ(0, rc);
    ASSERT_TRUE(server_mode_active());
    ASSERT_INT_EQ(port, server_mode_port());
    server_mode_stop();
    ASSERT_FALSE(server_mode_active());
    return 0;
}

static int test_double_start(void) {
    int port = pick_port();
    ASSERT_INT_EQ(0, server_mode_start(port));
    ASSERT_TRUE(server_mode_start(port + 1) != 0);
    server_mode_stop();
    return 0;
}

static int connect_to_server(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)server_mode_port());
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int rpc_exchange(int fd, const char *req, char *buf, int bufsz) {
    write(fd, req, strlen(req));
    server_mode_poll(100);
    usleep(50000);
    server_mode_poll(100);
    memset(buf, 0, (size_t)bufsz);
    ssize_t n = read(fd, buf, (size_t)(bufsz - 1));
    return (int)n;
}

static int test_client_connect(void) {
    int port = pick_port();
    ASSERT_INT_EQ(0, server_mode_start(port));

    int fd = connect_to_server();
    ASSERT_GE(fd, 0);
    server_mode_poll(100);

    char buf[1024];
    int n =
        rpc_exchange(fd, "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"id\":1}\n", buf, sizeof(buf));
    ASSERT_GT(n, 0);
    ASSERT_STR_CONTAINS(buf, "pong");

    close(fd);
    server_mode_stop();
    return 0;
}

static int test_rpc_status(void) {
    int port = pick_port();
    ASSERT_INT_EQ(0, server_mode_start(port));

    int fd = connect_to_server();
    ASSERT_GE(fd, 0);
    server_mode_poll(100);

    char buf[2048];
    rpc_exchange(fd, "{\"jsonrpc\":\"2.0\",\"method\":\"status\",\"id\":2}\n", buf, sizeof(buf));
    ASSERT_STR_CONTAINS(buf, "version");
    ASSERT_STR_CONTAINS(buf, NEVERCODE_VERSION);

    close(fd);
    server_mode_stop();
    return 0;
}

static int test_rpc_thread_lifecycle(void) {
    int port = pick_port();
    ASSERT_INT_EQ(0, server_mode_start(port));

    int fd = connect_to_server();
    ASSERT_GE(fd, 0);
    server_mode_poll(100);

    char buf[2048];
    rpc_exchange(fd,
                 "{\"jsonrpc\":\"2.0\",\"method\":\"thread/start\","
                 "\"params\":{\"label\":\"test-thread\"},\"id\":10}\n",
                 buf, sizeof(buf));
    ASSERT_STR_CONTAINS(buf, "test-thread");

    rpc_exchange(fd, "{\"jsonrpc\":\"2.0\",\"method\":\"thread/list\",\"id\":11}\n", buf,
                 sizeof(buf));
    ASSERT_STR_CONTAINS(buf, "threads");

    close(fd);
    server_mode_stop();
    return 0;
}

static int test_rpc_thread_start_uses_config_model(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.provider = PROVIDER_NATIVE;
    g_cfg_ptr = &cfg;

    int port = pick_port();
    ASSERT_INT_EQ(0, server_mode_start(port));

    thread_mgr_t *tm = thread_mgr_global();
    int before = tm->count;

    int fd = connect_to_server();
    ASSERT_GE(fd, 0);
    server_mode_poll(100);

    char buf[2048];
    rpc_exchange(fd,
                 "{\"jsonrpc\":\"2.0\",\"method\":\"thread/start\","
                 "\"params\":{\"label\":\"native-thread\"},\"id\":12}\n",
                 buf, sizeof(buf));
    ASSERT_STR_CONTAINS(buf, "native-thread");
    ASSERT_INT_EQ(tm->count, before + 1);
    ASSERT_NOT_NULL(tm->threads[before].model);
    ASSERT_STR_EQ(tm->threads[before].model, nc_provider_default_model(PROVIDER_NATIVE));

    close(fd);
    server_mode_stop();
    g_cfg_ptr = NULL;
    config_free(&cfg);
    return 0;
}

static int test_broadcast(void) {
    int port = pick_port();
    ASSERT_INT_EQ(0, server_mode_start(port));
    server_mode_broadcast("{\"event\":\"test\"}");
    server_mode_stop();
    return 0;
}

int test_server_mode_all(void) {
    TEST_SUITE("server_mode");
    TEST_COUNTERS;
    RUN_TEST(test_not_active);
    RUN_TEST(test_format);
    RUN_TEST(test_total_requests);
    RUN_TEST(test_port);
    RUN_TEST(test_start_stop);
    RUN_TEST(test_double_start);
    RUN_TEST(test_client_connect);
    RUN_TEST(test_rpc_status);
    RUN_TEST(test_rpc_thread_lifecycle);
    RUN_TEST(test_rpc_thread_start_uses_config_model);
    RUN_TEST(test_broadcast);
    TEST_RETURN_FAILED();
}
