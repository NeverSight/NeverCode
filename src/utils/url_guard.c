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
        {0xC01FC400, 24},  /* AS112 192.31.196.0/24 */
        {0xC034C100, 24},  /* AMT 192.52.193.0/24 */
        {0xC0586300, 24},  /* 6to4 relay anycast 192.88.99.0/24 */
        {0xC0AF3000, 24},  /* AS112 direct 192.175.48.0/24 */
        {0xE0000000, 4},   /* multicast 224/4 */
        {0xF0000000, 4},   /* reserved 240/4 */
    };
    for (size_t i = 0; i < sizeof(blocked) / sizeof(blocked[0]); i++)
        if (_v4_net_match(a, blocked[i].net, blocked[i].prefix))
            return false;
    return true;
}

/* Decode an embedded IPv4 from the last 4 bytes of the address. */
static uint32_t v4_from_tail(const unsigned char s[16]) {
    uint32_t v;
    memcpy(&v, s + 12, 4);   /* big-endian v4 literal; safe on all alignment */
    return v;                /* nc_ipv4_is_public expects network byte order */
}

/* True if `s` starts with the given 12-byte prefix. */
static bool v6_prefix(const unsigned char s[16], const unsigned char prefix[12]) {
    return memcmp(s, prefix, 12) == 0;
}

bool nc_ipv6_is_public(const unsigned char s[16]) {
    /* IPv4-compatible ::a.b.c.d (deprecated) — refuse wholesale */
    static const unsigned char ipv4_compat[12] = {0, 0, 0, 0, 0, 0,
                                                  0, 0, 0, 0, 0, 0};
    /* :: (unspecified), ::1 (loopback), and other ::/96 are covered below */

    /* IPv4-mapped ::ffff:a.b.c.d */
    static const unsigned char v4_mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    if (v6_prefix(s, v4_mapped))
        return nc_ipv4_is_public(v4_from_tail(s));

    /* ::/96 excluding mapped: unspecified, loopback, IPv4-compatible — refuse */
    if (v6_prefix(s, ipv4_compat))
        return false;

    /* NAT64 (RFC 6052) 64:ff9b::/96 — embedded IPv4 in the last 4 bytes */
    static const unsigned char nat64[12] = {0x00, 0x64, 0xff, 0x9b, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    if (v6_prefix(s, nat64))
        return nc_ipv4_is_public(v4_from_tail(s));

    /* Local-use NAT64 (RFC 8215) 64:ff9b:1::/48 — embedded IPv4 in the last 4 bytes */
    static const unsigned char nat64_local[16] = {0x00, 0x64, 0xff, 0x9b, 0x00, 0x01,
                                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                  0x00, 0x00, 0x00, 0x00};
    if (memcmp(s, nat64_local, 6) == 0) {
        uint32_t v;
        memcpy(&v, s + 12, 4);
        return nc_ipv4_is_public(v);
    }

    /* 6to4 2002::/16 — embedded IPv4 in bytes 2..5 */
    if (s[0] == 0x20 && s[1] == 0x02) {
        uint32_t v;
        memcpy(&v, s + 2, 4);
        return nc_ipv4_is_public(v);
    }

    /* fc00::/7 unique-local */
    if ((s[0] & 0xFE) == 0xFC)
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
    if (!address)
        return CURL_SOCKET_BAD;
    if (purpose == CURLSOCKTYPE_IPCXN) {
        if (!nc_sockaddr_is_public(address))
            return CURL_SOCKET_BAD;
    }
    curl_socket_t sock = socket(address->family, address->socktype, address->protocol);
    return sock;
}
