#include "test.h"
#include "nevercode.h"
#include "url_guard.h"
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

static int test_scheme_allowlist(void) {
    ASSERT(nc_url_scheme_http_https("http://example.com/"));
    ASSERT(nc_url_scheme_http_https("https://example.com/"));
    ASSERT(nc_url_scheme_http_https("HTTP://example.com/"));
    ASSERT(nc_url_scheme_http_https("HTTPS://example.com/path?q=1"));
    ASSERT(!nc_url_scheme_http_https("file:///etc/passwd"));
    ASSERT(!nc_url_scheme_http_https("ftp://example.com/"));
    ASSERT(!nc_url_scheme_http_https("gopher://example.com/"));
    ASSERT(!nc_url_scheme_http_https("javascript:alert(1)"));
    ASSERT(!nc_url_scheme_http_https(""));
    ASSERT(!nc_url_scheme_http_https(NULL));
    return 0;
}

static int test_ipv4_classes(void) {
    struct in_addr a;
    ASSERT(inet_pton(AF_INET, "8.8.8.8", &a) == 1);
    ASSERT(nc_ipv4_is_public(a.s_addr));
    ASSERT(inet_pton(AF_INET, "1.1.1.1", &a) == 1);
    ASSERT(nc_ipv4_is_public(a.s_addr));
    ASSERT(inet_pton(AF_INET, "93.184.216.34", &a) == 1);
    ASSERT(nc_ipv4_is_public(a.s_addr));
    ASSERT(inet_pton(AF_INET, "172.15.0.1", &a) == 1);
    ASSERT(nc_ipv4_is_public(a.s_addr));
    ASSERT(inet_pton(AF_INET, "172.32.0.1", &a) == 1);
    ASSERT(nc_ipv4_is_public(a.s_addr));

    static const char *blocked[] = {"0.0.0.0",       "10.0.0.1",      "10.255.255.255",
                                    "100.64.0.1",   "100.127.255.255", "127.0.0.1",
                                    "127.1.2.3",    "169.254.169.254",
                                    "172.16.0.1",   "172.31.255.255",
                                    "192.0.0.1",    "192.0.2.1",
                                    "192.168.0.1",  "198.18.0.1",
                                    "198.51.100.1", "203.0.113.1",
                                    "224.0.0.1",    "240.0.0.1",
                                    "255.255.255.255"};
    for (size_t i = 0; i < sizeof(blocked) / sizeof(blocked[0]); i++) {
        ASSERT(inet_pton(AF_INET, blocked[i], &a) == 1);
        ASSERT(!nc_ipv4_is_public(a.s_addr));
    }
    return 0;
}

static int test_ipv6_classes(void) {
    unsigned char b[16];
    ASSERT(inet_pton(AF_INET6, "::1", b) == 1);
    ASSERT(!nc_ipv6_is_public(b));
    ASSERT(inet_pton(AF_INET6, "::", b) == 1);
    ASSERT(!nc_ipv6_is_public(b));
    ASSERT(inet_pton(AF_INET6, "fe80::1", b) == 1);
    ASSERT(!nc_ipv6_is_public(b));
    ASSERT(inet_pton(AF_INET6, "fc00::1", b) == 1);
    ASSERT(!nc_ipv6_is_public(b));
    ASSERT(inet_pton(AF_INET6, "fd00::1", b) == 1);
    ASSERT(!nc_ipv6_is_public(b));
    ASSERT(inet_pton(AF_INET6, "ff02::1", b) == 1);
    ASSERT(!nc_ipv6_is_public(b));
    ASSERT(inet_pton(AF_INET6, "2001:db8::1", b) == 1);
    ASSERT(!nc_ipv6_is_public(b));
    /* IPv4-mapped loopback must be rejected */
    ASSERT(inet_pton(AF_INET6, "::ffff:127.0.0.1", b) == 1);
    ASSERT(!nc_ipv6_is_public(b));
    ASSERT(inet_pton(AF_INET6, "::ffff:10.0.0.1", b) == 1);
    ASSERT(!nc_ipv6_is_public(b));
    /* IPv4-mapped public */
    ASSERT(inet_pton(AF_INET6, "::ffff:8.8.8.8", b) == 1);
    ASSERT(nc_ipv6_is_public(b));
    ASSERT(inet_pton(AF_INET6, "2606:4700:4700::1111", b) == 1);
    ASSERT(nc_ipv6_is_public(b));
    return 0;
}

/* Mimic libcurl's allocation of curl_sockaddr with the full-sized sockaddr
 * embedded (family/protocol/addrlen header, then sockaddr_in6 payload). */
struct fake_sa {
    int family;
    int socktype;
    int protocol;
    unsigned int addrlen;
    struct sockaddr_in6 addr;
};

static int test_sockaddr_dispatch(void) {
    struct fake_sa sa;
    memset(&sa, 0, sizeof(sa));

    struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&sa.addr;
    sin6->sin6_family = AF_INET6;
    sa.family = AF_INET6;
    sa.addrlen = sizeof(struct sockaddr_in6);
    inet_pton(AF_INET6, "fe80::1", &sin6->sin6_addr);
    struct curl_sockaddr *p = (struct curl_sockaddr *)&sa;
    ASSERT(!nc_sockaddr_is_public(p));
    inet_pton(AF_INET6, "2606:4700:4700::1111", &sin6->sin6_addr);
    ASSERT(nc_sockaddr_is_public(p));

    /* IPv4 case: reuse same helper struct with v4 in place of v6 */
    struct fake_sa sa4;
    memset(&sa4, 0, sizeof(sa4));
    struct sockaddr_in *sin = (struct sockaddr_in *)&sa4.addr;
    sin->sin_family = AF_INET;
    sa4.family = AF_INET;
    sa4.addrlen = sizeof(struct sockaddr_in);
    inet_pton(AF_INET, "127.0.0.1", &sin->sin_addr);
    struct curl_sockaddr *p4 = (struct curl_sockaddr *)&sa4;
    ASSERT(!nc_sockaddr_is_public(p4));
    inet_pton(AF_INET, "93.184.216.34", &sin->sin_addr);
    ASSERT(nc_sockaddr_is_public(p4));

    memset(&sa, 0, sizeof(sa));
    sa.family = AF_UNIX;
    sa.addrlen = sizeof(struct sockaddr_un);
    ASSERT(!nc_sockaddr_is_public((struct curl_sockaddr *)&sa));
    return 0;
}

static int test_opensocket_refuses_private(void) {
    struct fake_sa sa;
    memset(&sa, 0, sizeof(sa));
    struct sockaddr_in *sin = (struct sockaddr_in *)&sa.addr;
    sin->sin_family = AF_INET;
    sa.family = AF_INET;
    sa.socktype = SOCK_STREAM;
    sa.addrlen = sizeof(struct sockaddr_in);
    struct curl_sockaddr *p = (struct curl_sockaddr *)&sa;
    inet_pton(AF_INET, "169.254.169.254", &sin->sin_addr);
    ASSERT_INT_EQ(nc_ssrf_opensocket(NULL, CURLSOCKTYPE_IPCXN, p), CURL_SOCKET_BAD);

    /* public address: a real socket is created (curl closes it itself) */
    inet_pton(AF_INET, "93.184.216.34", &sin->sin_addr);
    curl_socket_t s = nc_ssrf_opensocket(NULL, CURLSOCKTYPE_IPCXN, p);
    ASSERT(s != CURL_SOCKET_BAD);
    close(s);
    return 0;
}

int test_url_guard_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("url_guard");
    RUN_TEST(test_scheme_allowlist);
    RUN_TEST(test_ipv4_classes);
    RUN_TEST(test_ipv6_classes);
    RUN_TEST(test_sockaddr_dispatch);
    RUN_TEST(test_opensocket_refuses_private);
    TEST_RETURN_FAILED();
}
