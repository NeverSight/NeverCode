#include "nevercode.h"
#include "config.h"
#include "defaults.h"

typedef enum { R_IDLE = 0, R_CONN, R_STREAM, R_PAUSE, R_FLUSH, R_DONE, R_ERR } rp_t;

typedef struct {
    char *text;
    int tokens;
    long ts;
    bool final;
} rc_t;

typedef struct {
    rp_t phase;
    char sid[NC_TAG_MAX];
    char model[MODEL_NAME_MAX];
    rc_t chunks[RTC_MAX_CHUNKS];
    int cc;
    strbuf_t asm_buf;
    int ti, to;
    long sms;
    int total;
    pthread_mutex_t lock;
    bool init;
} rtc_t;

static rtc_t g;

void realtime_conv_init(void) {
    memset(&g, 0, sizeof(g));
    strbuf_init(&g.asm_buf);
    pthread_mutex_init(&g.lock, NULL);
    g.init = true;
}

void realtime_conv_free(void) {
    for (int i = 0; i < g.cc; i++)
        free(g.chunks[i].text);
    strbuf_free(&g.asm_buf);
    pthread_mutex_destroy(&g.lock);
    memset(&g, 0, sizeof(g));
}

int realtime_conv_begin(const char *s, const char *m) {
    if (!g.init)
        return -1;
    pthread_mutex_lock(&g.lock);
    for (int i = 0; i < g.cc; i++)
        free(g.chunks[i].text);
    g.cc = 0;
    strbuf_clear(&g.asm_buf);
    nc_strlcpy(g.sid, s ? s : "d", sizeof(g.sid));
    nc_strlcpy(g.model, m ? m : config_model(g_cfg_ptr), sizeof(g.model));
    g.phase = R_CONN;
    g.sms = nc_mono_ms();
    g.total++;
    pthread_mutex_unlock(&g.lock);
    return 0;
}

int realtime_conv_feed_chunk(const char *t, int tk, bool fin) {
    if (!g.init || g.phase >= R_DONE)
        return -1;
    pthread_mutex_lock(&g.lock);
    if (g.phase == R_CONN)
        g.phase = R_STREAM;
    if (g.cc < RTC_MAX_CHUNKS) {
        rc_t *c = &g.chunks[g.cc];
        c->text = t ? nc_xstrdup(t) : nc_xstrdup("");
        c->tokens = tk;
        c->ts = nc_wall_sec();
        c->final = fin;
        g.cc++;
    }
    if (t)
        strbuf_append_str(&g.asm_buf, t);
    g.to += tk;
    if (fin)
        g.phase = R_FLUSH;
    pthread_mutex_unlock(&g.lock);
    return 0;
}

int realtime_conv_complete(int it) {
    if (!g.init)
        return -1;
    pthread_mutex_lock(&g.lock);
    g.ti += it;
    g.phase = R_DONE;
    pthread_mutex_unlock(&g.lock);
    return 0;
}

int realtime_conv_error(const char *m) {
    if (!g.init)
        return -1;
    pthread_mutex_lock(&g.lock);
    g.phase = R_ERR;
    if (m && g.cc < RTC_MAX_CHUNKS) {
        rc_t *c = &g.chunks[g.cc];
        c->text = nc_xstrdup(m);
        c->final = true;
        g.cc++;
    }
    pthread_mutex_unlock(&g.lock);
    return 0;
}

char *realtime_conv_assembled(void) {
    return g.asm_buf.len > 0 ? nc_xstrdup(g.asm_buf.data) : nc_xstrdup("");
}

int realtime_conv_chunk_count(void) {
    return g.cc;
}
int realtime_conv_tokens_in(void) {
    return g.ti;
}
int realtime_conv_tokens_out(void) {
    return g.to;
}

char *realtime_conv_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    const char *pn[] = {"idle",     "connecting", "streaming", "paused",
                        "flushing", "complete",   "error"};
    strbuf_appendf(&sb,
                   "Realtime: phase=%s model=%s session=%s\n"
                   "  chunks=%d ti=%d to=%d sessions=%d\n",
                   pn[g.phase], g.model, g.sid, g.cc, g.ti, g.to, g.total);
    return strbuf_detach(&sb);
}
