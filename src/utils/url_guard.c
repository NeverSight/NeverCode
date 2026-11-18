/*
 * url_guard.c — Outbound HTTP(S) safety guard
 *
 * Two layers of SSRF defense for code paths that fetch user-influenced URLs:
 *
 *   1) Scheme allowlist: only http:// and https:// URLs may be fetched.
 *   2) Address-class filter at connect time: curl's OPENSOCKETFUNCTION is given
 *      every resolved address curl is about to connect to, so private,
 *      loopback, link-local, CGNAT, multicast and reserved addresses are
 *      refused before the socket is created. Because the check happens inside
 *      the connect path, redirects to internal hosts are refused too.
 */

#include "url_guard.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

bool nc_url_scheme_http_https(const char *url) {
    if (!url)
        return false;
    return strncasecmp(url, "http://", 7) == 0 || strncasecmp(url, "https://", 8) == 0;
}

static bool _v4_net_match(uint32_t addr, uint32_t net, int prefix) {
    uint32_t mask = prefix == 0 ? 0 : (~0u << (32 - prefix));
    return (addr & mask) == (net & mask);
}

bool nc_ipv4_is_public(uint32_t be_addr) {
    uint32_t a = ntohl(be_addr);
    static const struct {
        uint32_t net;
        int prefix;
    } blocked[] = {
        {0x00000000, 8},   /* "this network" */
        {0x0A000000, 8},   /* RFC1918 10/8 */
        {0x64400000, 10},  /* CGNAT 100.64/10 */
        {0x7F000000, 8},   /* loopback 127/8 */
        {0xA9FE0000, 16},  /* link-local 169.254/16 */
        {0xAC100000, 12},  /* RFC1918 172.16/12 */
        {0xC0000000, 24},  /* IETF protocol assignments 192.0.0.0/24 */
        {0xC0000200, 24},  /* TEST-NET-1 192.0.2.0/24 (documentation) */
        {0xC6336400, 24},  /* TEST-NET-2 198.51.100.0/24 */
        {0xCB007100, 24},  /* TEST-NET-3 203.0.113.0/24 */
        {0xC0A80000, 16},  /* RFC1918 192.168/16 */
        {0xC6120000, 15},  /* benchmarking 198.18/15 */
        {0xE0000000, 4},   /* multicast 224/4 */
        {0xF0000000, 4},   /* reserved 240/4 */
    };
    for (size_t i = 0; i < sizeof(blocked) / sizeof(blocked[0]); i++)
        if (_v4_net_match(a, blocked[i].net, blocked[i].prefix))
            return false;
    return true;
}

bool nc_ipv6_is_public(const unsigned char s[16]) {
    /* IPv4-mapped ::ffff:a.b.c.d */
    if (memcmp(s, "\0\0\0\0\0\0\0\0\0\0\xff\xff", 12) == 0)
        return nc_ipv4_is_public(*(const uint32_t *)(s + 12));
    /* :: (unspecified) */
    static const unsigned char zero16[16] = {0};
    if (memcmp(s, zero16, 16) == 0)
        return false;
    /* ::1 (loopback) */
    static const unsigned char loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                               0, 0, 0, 0, 0, 0, 0, 1};
    if (memcmp(s, loopback, 16) == 0)
        return false;
    if ((s[0] & 0xFE) == 0xFC) /* fc00::/7 unique-local */
        return false;
    if (s[0] == 0xFE && (s[1] & 0xC0) == 0x80) /* fe80::/10 link-local */
        return false;
    if (s[0] == 0xFE && (s[1] & 0xC0) == 0xC0) /* fec0::/10 site-local (deprecated) */
        return false;
    if (s[0] == 0xFF) /* ff00::/8 multicast */
        return false;
    /* 2001:db8::/32 documentation range */
    if (s[0] == 0x20 && s[1] == 0x01 && s[2] == 0x0D && s[3] == 0xB8)
        return false;
    /* ::/128 6bone, 2001::/32 Teredo tunneling can reach private space — block */
    if (s[0] == 0x20 && s[1] == 0x01 && s[2] == 0x00 && s[3] == 0x00)
        return false;
    return true;
}

bool nc_sockaddr_is_public(const struct curl_sockaddr *addr) {
    if (!addr)
        return false;

    /* libcurl stores a sockaddr_in/sockaddr_in6 at addr->addr with addrlen the
     * true length. Copy into a properly-sized storage container instead of
     * reading past the declared 16-byte sockaddr member. */
    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof(ss));
    unsigned int n = addr->addrlen;
    if (n > sizeof(ss))
        return false;
    if (n && addr->addr.sa_family != addr->family)
        return false;
    if (n)
        memcpy(&ss, &addr->addr, n);
    ss.ss_family = (sa_family_t)addr->family;

    if (ss.ss_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)&ss;
        return nc_ipv4_is_public(sin->sin_addr.s_addr);
    }
    if (ss.ss_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)&ss;
        return nc_ipv6_is_public(sin6->sin6_addr.s6_addr);
    }
    /* AF_UNIX and anything else: never allowed */
    return false;
}

/* curl OPENSOCKETFUNCTION: refuse to create a socket for a non-public address. */
curl_socket_t nc_ssrf_opensocket(void *clientp, curlsocktype purpose,
                                 struct curl_sockaddr *address) {
    (void)clientp;
    if (purpose == CURLSOCKTYPE_IPCXN) {
        if (!address || !nc_sockaddr_is_public(address))
            return CURL_SOCKET_BAD;
    }
    curl_socket_t sock = socket(address->family, address->socktype, address->protocol);
    return sock;
}
