#include "nevercode.h"
#include "defaults.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

typedef enum { B_OFF = 0, B_CONN, B_AUTH, B_READY, B_ACT, B_DRAIN, B_ERR } bs_t;

typedef struct {
    char sid[NC_TAG_MAX];
    char host[NC_LABEL_MAX];
    int port;
    int fd;
    bs_t state;
    long cat, bs, br;
    int ms, mr, rc;
    char err[NC_LABEL_MAX];
    pthread_mutex_t lock;
    bool ar;
} bc_t;

static struct {
    bc_t c[BRIDGE_MAX_CONNS];
    int n;
    bool init;
} gbr;

void remote_bridge_init(void) {
    memset(&gbr, 0, sizeof(gbr));
    for (int i = 0; i < BRIDGE_MAX_CONNS; i++) {
        gbr.c[i].fd = -1;
        pthread_mutex_init(&gbr.c[i].lock, NULL);
    }
    gbr.init = true;
}

void remote_bridge_free(void) {
    for (int i = 0; i < gbr.n; i++) {
        if (gbr.c[i].fd >= 0)
            close(gbr.c[i].fd);
        pthread_mutex_destroy(&gbr.c[i].lock);
    }
    memset(&gbr, 0, sizeof(gbr));
}

static int dial(const char *h, int p) {
    struct addrinfo hi, *r, *rp;
    memset(&hi, 0, sizeof(hi));
    hi.ai_family = AF_UNSPEC;
    hi.ai_socktype = SOCK_STREAM;
    char ps[NC_NUM_MAX];
    snprintf(ps, sizeof(ps), "%d", p);
    if (getaddrinfo(h, ps, &hi, &r) != 0)
        return -1;
    int fd = -1;
    for (rp = r; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(r);
    return fd;
}

int remote_bridge_connect(const char *h, int p, const char *s) {
    if (!gbr.init || !h || gbr.n >= BRIDGE_MAX_CONNS)
        return -1;
    int i = gbr.n;
    bc_t *c = &gbr.c[i];
    nc_strlcpy(c->host, h, sizeof(c->host));
    c->port = p;
    nc_strlcpy(c->sid, s ? s : "d", sizeof(c->sid));
    c->state = B_CONN;
    c->fd = dial(h, p);
    if (c->fd < 0) {
        snprintf(c->err, sizeof(c->err), "connect: %s", strerror(errno));
        c->state = B_ERR;
        gbr.n++;
        return -1;
    }
    c->cat = nc_wall_sec();
    c->state = B_READY;
    c->ar = true;
    gbr.n++;
    return i;
}

int remote_bridge_send(int i, const char *d, int l) {
    if (i < 0 || i >= gbr.n)
        return -1;
    bc_t *c = &gbr.c[i];
    if (c->state != B_READY && c->state != B_ACT)
        return -1;
    pthread_mutex_lock(&c->lock);
    c->state = B_ACT;
    char hdr[NC_NUM_MAX];
    int hl = snprintf(hdr, sizeof(hdr), "%08x\n", l);
    ssize_t w1 = write(c->fd, hdr, (size_t)hl);
    ssize_t w2 = write(c->fd, d, (size_t)l);
    pthread_mutex_unlock(&c->lock);
    if (w1 < 0 || w2 < 0) {
        c->state = B_ERR;
        return -1;
    }
    c->bs += w1 + w2;
    c->ms++;
    return 0;
}

int remote_bridge_recv(int i, char *buf, int bsz) {
    if (i < 0 || i >= gbr.n)
        return -1;
    bc_t *c = &gbr.c[i];
    if (c->state != B_READY && c->state != B_ACT)
        return -1;
    char hdr[NC_NUM_MAX];
    ssize_t hr = read(c->fd, hdr, 9);
    if (hr <= 0) {
        c->state = B_ERR;
        return -1;
    }
    hdr[hr] = '\0';
    int ex = (int)nc_safe_atol_hex(hdr, 0);
    if (ex <= 0 || ex >= bsz)
        return -1;
    int t = 0;
    while (t < ex) {
        ssize_t rd = read(c->fd, buf + t, (size_t)(ex - t));
        if (rd <= 0) {
            c->state = B_ERR;
            return -1;
        }
        t += (int)rd;
    }
    buf[t] = '\0';
    c->br += t;
    c->mr++;
    return t;
}

int remote_bridge_disconnect(int i) {
    if (i < 0 || i >= gbr.n)
        return -1;
    bc_t *c = &gbr.c[i];
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
    c->state = B_OFF;
    return 0;
}

int remote_bridge_count(void) {
    return gbr.n;
}

int remote_bridge_active_count(void) {
    int c = 0;
    for (int i = 0; i < gbr.n; i++)
        if (gbr.c[i].state == B_READY || gbr.c[i].state == B_ACT)
            c++;
    return c;
}

char *remote_bridge_format(int i) {
    if (i < 0 || i >= gbr.n)
        return nc_xstrdup("(invalid)");
    bc_t *c = &gbr.c[i];
    strbuf_t sb;
    strbuf_init(&sb);
    const char *sn[] = {"off", "connecting", "auth", "ready", "active", "draining", "error"};
    strbuf_appendf(&sb, "Bridge[%d] %s:%d sid=%s st=%s sent=%d/%ld recv=%d/%ld rc=%d\n", i, c->host,
                   c->port, c->sid, sn[c->state], c->ms, c->bs, c->mr, c->br, c->rc);
    if (c->err[0])
        strbuf_appendf(&sb, "  err: %s\n", c->err);
    return strbuf_detach(&sb);
}

char *remote_bridge_format_all(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Remote Bridge: %d connections (%d active)\n", gbr.n,
                   remote_bridge_active_count());
    for (int i = 0; i < gbr.n; i++) {
        char *s = remote_bridge_format(i);
        strbuf_append_str(&sb, s);
        free(s);
    }
    return strbuf_detach(&sb);
}
