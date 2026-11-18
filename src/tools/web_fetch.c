#include "tools.h"
#include "nevercode.h"
#include "url_guard.h"
#include <curl/curl.h>
#include <iconv.h>

#define MAX_FETCH WEBFETCH_MAX_BYTES

static const char *SCHEMA = "{"
                            "\"type\":\"object\","
                            "\"properties\":{"
                            "\"url\":{\"type\":\"string\",\"description\":\"URL to fetch\"}"
                            "},"
                            "\"required\":[\"url\"]"
                            "}";

static size_t fetch_write_cb(char *ptr, size_t sz, size_t n, void *ud) {
    strbuf_t *sb = ud;
    size_t bytes = sz * n;
    if (sb->len + bytes > MAX_FETCH) {
        size_t room = MAX_FETCH - sb->len;
        if (room > 0)
            strbuf_append(sb, ptr, room);
        return bytes;
    }
    strbuf_append(sb, ptr, bytes);
    return bytes;
}

static void decode_entities(strbuf_t *out, const char *text) {
    for (const char *p = text; *p;) {
        if (*p != '&') {
            strbuf_append(out, p, 1);
            p++;
            continue;
        }
        const char *semi = strchr(p, ';');
        if (!semi || semi - p > 10) {
            strbuf_append(out, p, 1);
            p++;
            continue;
        }

        size_t elen = semi - p - 1;
        const char *ent = p + 1;
        char decoded = 0;
        if (elen == 3 && strncmp(ent, "amp", 3) == 0)
            decoded = '&';
        else if (elen == 2 && strncmp(ent, "lt", 2) == 0)
            decoded = '<';
        else if (elen == 2 && strncmp(ent, "gt", 2) == 0)
            decoded = '>';
        else if (elen == 4 && strncmp(ent, "quot", 4) == 0)
            decoded = '"';
        else if (elen == 4 && strncmp(ent, "apos", 4) == 0)
            decoded = '\'';
        else if (elen == 4 && strncmp(ent, "nbsp", 4) == 0)
            decoded = ' ';
        else if (ent[0] == '#') {
            int codepoint = 0;
            if (ent[1] == 'x' || ent[1] == 'X')
                codepoint = (int)nc_safe_atol_hex(ent + 2, 0);
            else
                codepoint = nc_safe_atoi(ent + 1, 0);
            if (codepoint > 0 && codepoint < 128)
                decoded = (char)codepoint;
        }

        if (decoded) {
            strbuf_append(out, &decoded, 1);
            p = semi + 1;
        } else {
            strbuf_append(out, p, 1);
            p++;
        }
    }
}

static void strip_tags(strbuf_t *out, const char *html) {
    strbuf_t raw;
    strbuf_init(&raw);
    bool in_tag = false;
    bool in_skip = false;
    for (const char *p = html; *p; p++) {
        if (in_skip) {
            if (strncasecmp(p, "</script", 8) == 0 || strncasecmp(p, "</style", 7) == 0 ||
                strncasecmp(p, "</noscript", 10) == 0)
                in_skip = false;
            continue;
        }
        if (*p == '<') {
            if (strncasecmp(p, "<script", 7) == 0 || strncasecmp(p, "<style", 6) == 0 ||
                strncasecmp(p, "<noscript", 9) == 0)
                in_skip = true;
            in_tag = true;
            continue;
        }
        if (*p == '>') {
            in_tag = false;
            continue;
        }
        if (!in_tag)
            strbuf_append(&raw, p, 1);
    }
    if (raw.data)
        decode_entities(out, raw.data);
    strbuf_free(&raw);
}

/*
 * Detect charset from Content-Type header value or HTML <meta> tag.
 * Writes a normalized uppercase charset name into `out` (e.g. "GB2312").
 * Returns true if a non-UTF-8 charset was found.
 */
static bool detect_charset(const char *content_type, const char *body, char *out, size_t outsz) {
    out[0] = '\0';
    const char *src = NULL;

    if (content_type) {
        const char *p = strcasestr(content_type, "charset=");
        if (p) {
            src = p + 8;
        }
    }

    if (!src && body) {
        const char *limit = body + (strlen(body) > NC_PATH_MAX ? NC_PATH_MAX : strlen(body));
        for (const char *p = body; p < limit; p++) {
            if (strncasecmp(p, "charset=", 8) == 0 || strncasecmp(p, "charset =", 9) == 0) {
                p = strchr(p, '=') + 1;
                while (*p == ' ' || *p == '"' || *p == '\'')
                    p++;
                src = p;
                break;
            }
        }
    }

    if (!src)
        return false;

    size_t i = 0;
    while (i < outsz - 1 && src[i] && src[i] != '"' && src[i] != '\'' && src[i] != ';' &&
           src[i] != ' ' && src[i] != '>' && src[i] != '\n')
        i++;
    if (i == 0)
        return false;
    memcpy(out, src, i);
    out[i] = '\0';

    for (size_t j = 0; j < i; j++)
        out[j] = (char)toupper((unsigned char)out[j]);

    if (strcmp(out, "UTF-8") == 0 || strcmp(out, "UTF8") == 0)
        return false;
    return true;
}

static char *convert_encoding(const char *data, size_t len, const char *from_charset) {
    iconv_t cd = iconv_open("UTF-8", from_charset);
    if (cd == (iconv_t)-1)
        return NULL;

    size_t out_alloc = len * 4 + 4;
    char *out = malloc(out_alloc);
    if (!out) {
        iconv_close(cd);
        return NULL;
    }

    char *in_ptr = (char *)data;
    size_t in_left = len;
    char *out_ptr = out;
    size_t out_left = out_alloc - 1;

    iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
    *out_ptr = '\0';
    iconv_close(cd);
    return out;
}

static int web_fetch_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *url = nc_cjson_str((cJSON *)input, "url", NULL);
    if (!url) {
        *output = nc_xstrdup("error: missing 'url'");
        return 1;
    }
    if (!nc_url_scheme_http_https(url)) {
        *output = nc_xstrdup("error: only http:// and https:// URLs are allowed");
        return 1;
    }

    strbuf_t sb;
    strbuf_init(&sb);

    CURL *curl = curl_easy_init();
    if (!curl) {
        strbuf_free(&sb);
        *output = nc_xstrdup("error: curl init failed");
        return 1;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fetch_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)WEBFETCH_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)NC_CURL_CONNECT_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, (long)NC_CURL_MAX_REDIRS);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, nc_ssrf_opensocket);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                     "AppleWebKit/537.36 (KHTML, like Gecko) "
                     "Chrome/131.0.0.0 Safari/537.36");

    const char *proxy = nc_get_proxy_env();
    if (proxy)
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy);

    char *content_type_hdr = NULL;
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type_hdr);
    /* content_type_hdr points into curl internals — copy before cleanup */
    char ct_buf[NC_LABEL_MAX] = "";
    if (content_type_hdr)
        nc_strlcpy(ct_buf, content_type_hdr, sizeof(ct_buf));
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        strbuf_free(&sb);
        char msg[NC_MSG_MAX];
        snprintf(msg, sizeof(msg), "error: %s", curl_easy_strerror(res));
        *output = nc_xstrdup(msg);
        return 1;
    }

    /* Charset conversion (GB2312, GBK, Shift_JIS, EUC-KR, etc.) */
    char charset[NC_FMT_MAX];
    char *converted = NULL;
    if (sb.data && detect_charset(ct_buf[0] ? ct_buf : NULL, sb.data, charset, sizeof(charset))) {
        converted = convert_encoding(sb.data, sb.len, charset);
    }
    const char *body = converted ? converted : (sb.data ? sb.data : "");

    strbuf_t text;
    strbuf_init(&text);
    char header[NC_TAG_MAX];
    snprintf(header, sizeof(header), "[HTTP %ld] %s\n\n", code, url);
    strbuf_append_str(&text, header);

    if (strstr(body, "<html") || strstr(body, "<HTML") || strstr(body, "<!DOCTYPE") ||
        strstr(body, "<!doctype")) {
        strip_tags(&text, body);
    } else {
        strbuf_append_str(&text, body);
    }

    free(converted);
    strbuf_free(&sb);
    *output = strbuf_detach(&text);
    return 0;
}

static tool_def_t web_fetch_tool;

void tool_web_fetch_register(void) {
    web_fetch_tool.name = "WebFetch";
    web_fetch_tool.description = "Fetch a webpage by URL and return its text content. "
                                 "HTML tags are stripped automatically.";
    web_fetch_tool.input_schema = cJSON_Parse(SCHEMA);
    web_fetch_tool.execute = web_fetch_exec;
    web_fetch_tool.read_only = true;
    web_fetch_tool.concurrency_safe = true;
    web_fetch_tool.destructive = false;
    tools_register(&web_fetch_tool);
}
