/*
 * request_compress.c — API request body compression.
 *
 * Compresses large API request payloads with gzip to reduce bandwidth
 * and improve latency on slow connections. Automatically activates when
 * the payload exceeds a configurable threshold.
 */

#include "nevercode.h"
#include <zlib.h>

#define COMPRESS_THRESHOLD RC_DEFAULT_THRESHOLD
#define COMPRESS_LEVEL RC_DEFAULT_LEVEL
#define GZIP_WINDOW_BITS (15 + 16)
/* RC_BUF_EXTRA in defaults.h */

static struct {
    bool enabled;
    int threshold;
    int level;
    long total_original;
    long total_compressed;
    int total_calls;
    int total_skipped;
} g_rc = {.enabled = true, .threshold = COMPRESS_THRESHOLD, .level = COMPRESS_LEVEL};

void request_compress_init(void) {
    g_rc.enabled = true;
    g_rc.threshold = COMPRESS_THRESHOLD;
    g_rc.level = COMPRESS_LEVEL;
    g_rc.total_original = 0;
    g_rc.total_compressed = 0;
    g_rc.total_calls = 0;
    g_rc.total_skipped = 0;
}

void request_compress_auto_configure(const char *base_url) {
    if (!base_url)
        return;
    if (strstr(base_url, DEFAULT_COMPAT_HOST) || strstr(base_url, DEFAULT_NATIVE_HOST))
        return;
    g_rc.enabled = false;
}

void request_compress_set_enabled(bool on) {
    g_rc.enabled = on;
}

bool request_compress_enabled(void) {
    return g_rc.enabled;
}

void request_compress_set_threshold(int bytes) {
    if (bytes > 0)
        g_rc.threshold = bytes;
}

void request_compress_set_level(int level) {
    if (level >= 1 && level <= 9)
        g_rc.level = level;
}

int request_compress_gzip(const char *input, size_t input_len, char **output, size_t *output_len) {
    if (!input || !output || !output_len || input_len == 0)
        return -1;

    size_t bound = compressBound((uLong)input_len) + RC_BUF_EXTRA;
    *output = malloc(bound);
    if (!*output)
        return -1;

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, g_rc.level, Z_DEFLATED, GZIP_WINDOW_BITS, 8, Z_DEFAULT_STRATEGY) !=
        Z_OK) {
        free(*output);
        *output = NULL;
        return -1;
    }

    zs.next_in = (Bytef *)input;
    zs.avail_in = (uInt)input_len;
    zs.next_out = (Bytef *)*output;
    zs.avail_out = (uInt)bound;

    int rc = deflate(&zs, Z_FINISH);
    deflateEnd(&zs);

    if (rc != Z_STREAM_END) {
        free(*output);
        *output = NULL;
        return -1;
    }

    *output_len = zs.total_out;
    g_rc.total_original += (long)input_len;
    g_rc.total_compressed += (long)*output_len;
    g_rc.total_calls++;
    return 0;
}

bool request_compress_should_compress(size_t payload_size) {
    return g_rc.enabled && (int)payload_size >= g_rc.threshold;
}

int request_compress_maybe(const char *body, size_t body_len, char **out_body, size_t *out_len,
                           bool *was_compressed) {
    *was_compressed = false;
    if (!request_compress_should_compress(body_len)) {
        g_rc.total_skipped++;
        *out_body = NULL;
        *out_len = body_len;
        return 0;
    }

    int rc = request_compress_gzip(body, body_len, out_body, out_len);
    if (rc == 0)
        *was_compressed = true;
    return rc;
}

double request_compress_ratio(void) {
    if (g_rc.total_original == 0)
        return 1.0;
    return (double)g_rc.total_compressed / (double)g_rc.total_original;
}

long request_compress_saved_bytes(void) {
    return g_rc.total_original - g_rc.total_compressed;
}

int request_compress_call_count(void) {
    return g_rc.total_calls;
}

int request_compress_skip_count(void) {
    return g_rc.total_skipped;
}

char *request_compress_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Compression: %s threshold=%d level=%d\n", g_rc.enabled ? "on" : "off",
                   g_rc.threshold, g_rc.level);
    strbuf_appendf(&sb, "  calls=%d skipped=%d ratio=%.1f%% saved=%ldKB\n", g_rc.total_calls,
                   g_rc.total_skipped, request_compress_ratio() * 100.0,
                   request_compress_saved_bytes() / 1024);
    return strbuf_detach(&sb);
}
