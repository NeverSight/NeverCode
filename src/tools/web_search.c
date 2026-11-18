#include "tools.h"
#include "nevercode.h"
#include "url_guard.h"
#include <curl/curl.h>

#define MAX_SEARCH_OUTPUT WEBSEARCH_MAX_OUTPUT

static const char *SCHEMA = "{"
                            "\"type\":\"object\","
                            "\"properties\":{"
                            "\"query\":{\"type\":\"string\",\"description\":\"Search query\"}"
                            "},"
                            "\"required\":[\"query\"]"
                            "}";

static size_t search_write_cb(char *ptr, size_t sz, size_t n, void *ud) {
    strbuf_t *sb = ud;
    size_t bytes = sz * n;
    if (sb->len < MAX_SEARCH_OUTPUT)
        strbuf_append(sb, ptr, bytes);
    return bytes;
}

static void parse_ddg_results(strbuf_t *out, const char *html) {
    int count = 0;
    const char *p = html;

    while (count < WEBSEARCH_MAX_RESULTS && (p = strstr(p, "class=\"result__a\"")) != NULL) {
        const char *href_start = NULL;
        const char *scan = p;
        for (int i = 0; i < WEBSEARCH_HREF_SCAN_MAX && scan > html; i++, scan--) {
            if (nc_starts_with(scan, "href=\"")) {
                href_start = scan + 6;
                break;
            }
        }

        const char *tag_end = strchr(p, '>');
        if (!tag_end)
            break;
        tag_end++;

        strbuf_t title;
        strbuf_init(&title);
        const char *close = strstr(tag_end, "</a>");
        if (close) {
            bool in_t = false;
            for (const char *c = tag_end; c < close; c++) {
                if (*c == '<')
                    in_t = true;
                else if (*c == '>')
                    in_t = false;
                else if (!in_t)
                    strbuf_append(&title, c, 1);
            }
        }

        strbuf_t url;
        strbuf_init(&url);
        if (href_start) {
            const char *hend = strchr(href_start, '"');
            if (hend)
                strbuf_append(&url, href_start, hend - href_start);
        }

        strbuf_t snippet;
        strbuf_init(&snippet);
        const char *snip = strstr(p, "class=\"result__snippet\"");
        if (snip) {
            const char *se = strchr(snip, '>');
            if (se) {
                se++;
                const char *sclose = strstr(se, "</a>");
                if (!sclose)
                    sclose = strstr(se, "</span>");
                if (sclose) {
                    bool in_t = false;
                    for (const char *c = se; c < sclose; c++) {
                        if (*c == '<')
                            in_t = true;
                        else if (*c == '>')
                            in_t = false;
                        else if (!in_t)
                            strbuf_append(&snippet, c, 1);
                    }
                }
            }
        }

        if (title.len > 0) {
            count++;
            char num[NC_NUM_MAX];
            snprintf(num, sizeof(num), "%d. ", count);
            strbuf_append_str(out, num);
            strbuf_append_str(out, title.data);
            strbuf_append_str(out, "\n");
            if (url.len > 0) {
                strbuf_append_str(out, "   ");
                strbuf_append_str(out, url.data);
                strbuf_append_str(out, "\n");
            }
            if (snippet.len > 0) {
                strbuf_append_str(out, "   ");
                strbuf_append_str(out, snippet.data);
                strbuf_append_str(out, "\n");
            }
            strbuf_append_str(out, "\n");
        }

        strbuf_free(&title);
        strbuf_free(&url);
        strbuf_free(&snippet);
        p = close ? close : tag_end;
    }

    if (count == 0)
        strbuf_append_str(out, "(no results found)\n");
}

static int web_search_exec(const cJSON *input, char **output, void *ctx) {
    (void)ctx;
    const char *query = nc_cjson_str((cJSON *)input, "query", NULL);
    if (!query) {
        *output = nc_xstrdup("error: missing 'query'");
        return 1;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        *output = nc_xstrdup("error: curl init failed");
        return 1;
    }
    char *eq = curl_easy_escape(curl, query, 0);

    char url[NC_PATH_MAX];
    snprintf(url, sizeof(url), WEBSEARCH_URL, eq);
    curl_free(eq);

    strbuf_t sb;
    strbuf_init(&sb);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, search_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)WEBSEARCH_TIMEOUT_S);
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
                     "Mozilla/5.0 (compatible; nevercode/" NEVERCODE_VERSION ")");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const char *proxy = nc_get_proxy_env();
    if (proxy)
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        strbuf_free(&sb);
        char msg[NC_MSG_MAX];
        snprintf(msg, sizeof(msg), "error: %s", curl_easy_strerror(res));
        *output = nc_xstrdup(msg);
        return 1;
    }

    strbuf_t text;
    strbuf_init(&text);
    char header[NC_LABEL_MAX];
    snprintf(header, sizeof(header), "Search results for: %s\n\n", query);
    strbuf_append_str(&text, header);

    if (sb.data)
        parse_ddg_results(&text, sb.data);
    strbuf_free(&sb);

    *output = strbuf_detach(&text);
    return 0;
}

static tool_def_t web_search_tool;

void tool_web_search_register(void) {
    web_search_tool.name = "WebSearch";
    web_search_tool.description = "Search the web for information. Returns text results "
                                  "from DuckDuckGo.";
    web_search_tool.input_schema = cJSON_Parse(SCHEMA);
    web_search_tool.execute = web_search_exec;
    web_search_tool.read_only = true;
    web_search_tool.concurrency_safe = true;
    web_search_tool.destructive = false;
    tools_register(&web_search_tool);
}
