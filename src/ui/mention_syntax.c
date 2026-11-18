/*
 * mention_syntax.c — Enhanced @-mention parsing for user input
 *
 * Extends inline content expansion with:
 *   @url   — auto-fetch URL content via libcurl
 *   @~path — tilde expansion
 *   @./rel — explicit relative path
 *   @dir/  — directory listing inline
 */

#include "nevercode.h"
#include "git_ops.h"
#include "url_guard.h"
#include <curl/curl.h>

#define MAX_URL_FETCH_SIZE URL_FETCH_MAX_BYTES

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} fetch_buf_t;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *ud) {
    fetch_buf_t *fb = ud;
    size_t total = size * nmemb;
    if (fb->len + total > MAX_URL_FETCH_SIZE) {
        size_t remain = MAX_URL_FETCH_SIZE - fb->len;
        if (remain == 0)
            return 0;
        total = remain;
    }
    if (fb->len + total >= fb->cap) {
        size_t nc = (fb->len + total) * 2 + 256;
        char *tmp = realloc(fb->data, nc);
        if (!tmp)
            return 0;
        fb->data = tmp;
        fb->cap = nc;
    }
    memcpy(fb->data + fb->len, ptr, total);
    fb->len += total;
    return size * nmemb;
}

static bool is_url_char(char c) {
    return isalnum((unsigned char)c) || c == '/' || c == '.' || c == '-' || c == '_' || c == ':' ||
           c == '?' || c == '&' || c == '=' || c == '%' || c == '#' || c == '+' || c == '~' ||
           c == '@' || c == '!' || c == '$' || c == '(' || c == ')' || c == ',';
}

bool mention_is_url(const char *s) {
    return nc_starts_with(s, "http://") || nc_starts_with(s, "https://");
}

char *mention_fetch_url(const char *url) {
    if (!url)
        return NULL;

    if (!nc_url_scheme_http_https(url))
        return NULL; /* only http/https URLs may be fetched */

    CURL *curl = curl_easy_init();
    if (!curl)
        return NULL;

    fetch_buf_t fb = {0};
    fb.cap = FETCH_BUF_INITIAL;
    fb.data = malloc(fb.cap);
    if (!fb.data) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fb);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(curl, CURLOPT_OPENSOCKETFUNCTION, nc_ssrf_opensocket);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)WEBSEARCH_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)NC_CURL_CONNECT_SHORT_S);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, NC_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, (long)NC_CURL_MAX_REDIRS);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const char *proxy = nc_get_proxy_env();
    if (proxy)
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || status < 200 || status >= 400) {
        free(fb.data);
        return NULL;
    }

    if (fb.len > 0) {
        fb.data[fb.len < fb.cap ? fb.len : fb.cap - 1] = '\0';
    } else {
        free(fb.data);
        return NULL;
    }

    return fb.data;
}

static char *expand_tilde(const char *path) {
    if (!path || path[0] != '~')
        return NULL;
    const char *home = nc_home_dir();
    if (!home)
        return NULL;

    if (path[1] == '/' || path[1] == '\0') {
        size_t sz = strlen(home) + strlen(path) + 1;
        char *result = malloc(sz);
        if (!result)
            return NULL;
        snprintf(result, sz, "%s%s", home, path + 1);
        return result;
    }
    return NULL;
}

static char *list_directory(const char *dirpath, int max_entries) {
    DIR *d = opendir(dirpath);
    if (!d)
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_str(&sb, "Directory listing of ");
    strbuf_append_str(&sb, dirpath);
    strbuf_append_str(&sb, ":\n");

    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(d)) && count < max_entries) {
        if (ent->d_name[0] == '.')
            continue;

        char fullpath[NC_PATH_MAX];
        snprintf(fullpath, sizeof(fullpath), "%.2045s/%.2045s", dirpath, ent->d_name);
        struct stat st;
        bool is_dir = (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode));

        strbuf_append_str(&sb, "  ");
        strbuf_append_str(&sb, ent->d_name);
        if (is_dir)
            strbuf_append_str(&sb, "/");
        strbuf_append_str(&sb, "\n");
        count++;
    }

    if (count >= max_entries)
        strbuf_append_str(&sb, "  ... (truncated)\n");

    closedir(d);
    return strbuf_detach(&sb);
}

static bool is_mention_path_char(char c) {
    return isalnum((unsigned char)c) || c == '/' || c == '.' || c == '-' || c == '_' || c == '~' ||
           c == '+' || c == ':';
}

/* Parse optional :START or :START-END range suffix from a path.
 * Returns the portion of `raw` that is the actual path (everything before
 * the colon-range), and fills out_start/out_end if a range was found.
 * out_end == out_start when only a single line number was given. */
static size_t parse_line_range(const char *raw, size_t raw_len, int *out_start, int *out_end) {
    *out_start = 0;
    *out_end = 0;
    if (raw_len < 3)
        return raw_len;
    /* Scan backwards for the last ':' that is followed by a digit */
    for (size_t i = raw_len - 1; i > 0; i--) {
        if (raw[i] == ':' && i + 1 < raw_len && raw[i + 1] >= '0' && raw[i + 1] <= '9') {
            int s = 0, e = 0;
            const char *num = raw + i + 1;
            while (*num >= '0' && *num <= '9')
                s = s * 10 + (*num++ - '0');
            if (*num == '-' && *(num + 1) >= '0' && *(num + 1) <= '9') {
                num++;
                while (*num >= '0' && *num <= '9')
                    e = e * 10 + (*num++ - '0');
            } else {
                e = s;
            }
            if (s > 0 && e >= s) {
                *out_start = s;
                *out_end = e;
                return i;
            }
            break;
        }
    }
    return raw_len;
}

static char *run_git_alloc(bool staged) {
    return git_cmd_alloc(NULL, (const char *const[]){"diff", staged ? "--cached" : "HEAD"}, 2,
                         DIFF_OUTPUT_MAX_BYTES);
}

static bool is_git_keyword(const char *ref, const char *kw) {
    size_t kl = strlen(kw);
    return strncmp(ref, kw, kl) == 0 && (ref[kl] == '\0' || isspace((unsigned char)ref[kl]) ||
                                         ref[kl] == ',' || ref[kl] == '.' || ref[kl] == ')');
}

char *mention_parse_enhanced(const char *input) {
    if (!input || !strchr(input, '@'))
        return NULL;

    strbuf_t sb;
    strbuf_init(&sb);
    bool injected = false;
    const char *seg_start = input;

    const char *p = input;
    while (*p) {
        if (*p == '@' && (p == input || isspace((unsigned char)*(p - 1)))) {
            const char *ref = p + 1;

            if (is_git_keyword(ref, "diff")) {
                char *diff = run_git_alloc(false);
                if (diff) {
                    strbuf_append(&sb, seg_start, p - seg_start);
                    strbuf_append_str(&sb, "\n\n<git-diff>\n");
                    size_t dlen = strlen(diff);
                    if (dlen > MENTION_CONTENT_MAX) {
                        strbuf_append(&sb, diff, MENTION_CONTENT_MAX);
                        strbuf_append_str(&sb, "\n...(truncated)");
                    } else {
                        strbuf_append_str(&sb, diff);
                    }
                    strbuf_append_str(&sb, "\n</git-diff>\n\n");
                    free(diff);
                    seg_start = ref + 4;
                    p = seg_start;
                    injected = true;
                    continue;
                }
            }

            if (is_git_keyword(ref, "staged")) {
                char *diff = run_git_alloc(true);
                if (diff) {
                    strbuf_append(&sb, seg_start, p - seg_start);
                    strbuf_append_str(&sb, "\n\n<git-staged>\n");
                    size_t dlen = strlen(diff);
                    if (dlen > MENTION_CONTENT_MAX) {
                        strbuf_append(&sb, diff, MENTION_CONTENT_MAX);
                        strbuf_append_str(&sb, "\n...(truncated)");
                    } else {
                        strbuf_append_str(&sb, diff);
                    }
                    strbuf_append_str(&sb, "\n</git-staged>\n\n");
                    free(diff);
                    seg_start = ref + 6;
                    p = seg_start;
                    injected = true;
                    continue;
                }
            }

            if (mention_is_url(ref)) {
                const char *url_end = ref;
                while (*url_end && is_url_char(*url_end))
                    url_end++;

                size_t url_len = url_end - ref;
                char *url = malloc(url_len + 1);
                if (!url)
                    break;
                memcpy(url, ref, url_len);
                url[url_len] = '\0';

                char *content = mention_fetch_url(url);
                if (content) {
                    strbuf_append(&sb, seg_start, p - seg_start);
                    strbuf_append_str(&sb, "\n\n<url href=\"");
                    strbuf_append_str(&sb, url);
                    strbuf_append_str(&sb, "\">\n");

                    size_t clen = strlen(content);
                    if (clen > MENTION_CONTENT_MAX) {
                        strbuf_append(&sb, content, MENTION_CONTENT_MAX);
                        strbuf_append_str(&sb, "\n...(truncated)");
                    } else {
                        strbuf_append_str(&sb, content);
                    }
                    strbuf_append_str(&sb, "\n</url>\n\n");
                    free(content);
                    seg_start = url_end;
                    p = url_end;
                    injected = true;
                }
                free(url);
                if (injected && p == url_end)
                    continue;
            }

            const char *path_end = ref;
            while (*path_end && is_mention_path_char(*path_end))
                path_end++;
            size_t path_len = path_end - ref;

            if (path_len > 1 && path_len < NC_PATH_MAX) {
                char raw_path[NC_PATH_MAX];
                memcpy(raw_path, ref, path_len);
                raw_path[path_len] = '\0';

                int range_start = 0, range_end = 0;
                size_t actual_path_len =
                    parse_line_range(raw_path, path_len, &range_start, &range_end);
                char actual_path[NC_PATH_MAX];
                memcpy(actual_path, raw_path, actual_path_len);
                actual_path[actual_path_len] = '\0';

                char *resolved = NULL;
                if (actual_path[0] == '~')
                    resolved = expand_tilde(actual_path);

                const char *check_path = resolved ? resolved : actual_path;
                struct stat st;
                if (stat(check_path, &st) == 0) {
                    strbuf_append(&sb, seg_start, p - seg_start);

                    if (S_ISDIR(st.st_mode)) {
                        char *listing = list_directory(check_path, 50);
                        if (listing) {
                            strbuf_append_str(&sb, "\n\n<directory path=\"");
                            strbuf_append_str(&sb, check_path);
                            strbuf_append_str(&sb, "\">\n");
                            strbuf_append_str(&sb, listing);
                            strbuf_append_str(&sb, "</directory>\n\n");
                            free(listing);
                        }
                    } else if (S_ISREG(st.st_mode)) {
                        if (is_image_file(check_path)) {
                            strbuf_append_str(&sb, "[image: ");
                            strbuf_append_str(&sb, check_path);
                            strbuf_append_str(&sb, "]");
                        } else if (st.st_size > INLINE_FILE_SIZE_MAX) {
                            char msg[NC_LABEL_MAX];
                            snprintf(msg, sizeof(msg), "[file too large: %lld bytes]",
                                     (long long)st.st_size);
                            strbuf_append_str(&sb, msg);
                        } else {
                            FILE *f = fopen(check_path, "r");
                            if (f) {
                                strbuf_append_str(&sb, "\n\n<file path=\"");
                                strbuf_append_str(&sb, check_path);
                                if (range_start > 0) {
                                    char rangebuf[NC_FMT_MAX];
                                    snprintf(rangebuf, sizeof(rangebuf), "\" lines=\"%d-%d",
                                             range_start, range_end);
                                    strbuf_append_str(&sb, rangebuf);
                                }
                                strbuf_append_str(&sb, "\">\n");

                                if (range_start > 0) {
                                    char line[NC_LINE_MAX];
                                    int lineno = 0;
                                    while (fgets(line, sizeof(line), f)) {
                                        lineno++;
                                        if (lineno > range_end)
                                            break;
                                        if (lineno >= range_start)
                                            strbuf_append_str(&sb, line);
                                    }
                                } else {
                                    char *buf = malloc(st.st_size + 1);
                                    if (!buf) {
                                        fclose(f);
                                        break;
                                    }
                                    size_t n = fread(buf, 1, st.st_size, f);
                                    buf[n] = '\0';
                                    strbuf_append_str(&sb, buf);
                                    free(buf);
                                }
                                fclose(f);
                                strbuf_append_str(&sb, "\n</file>\n\n");
                            }
                        }
                    }

                    seg_start = path_end;
                    p = path_end;
                    injected = true;
                    free(resolved);
                    continue;
                }
                free(resolved);
            }
        }
        p++;
    }

    if (!injected) {
        strbuf_free(&sb);
        return NULL;
    }

    strbuf_append_str(&sb, seg_start);
    return strbuf_detach(&sb);
}

int mention_count_refs(const char *input) {
    if (!input)
        return 0;
    int count = 0;
    const char *p = input;
    while (*p) {
        if (*p == '@' && (p == input || isspace((unsigned char)*(p - 1)))) {
            const char *ref = p + 1;
            if (mention_is_url(ref)) {
                count++;
            } else {
                const char *end = ref;
                while (*end && is_mention_path_char(*end))
                    end++;
                if (end - ref > 1) {
                    char path[NC_PATH_MAX];
                    size_t pl = end - ref;
                    if (pl < sizeof(path)) {
                        memcpy(path, ref, pl);
                        path[pl] = '\0';
                        struct stat st;
                        char *resolved = (path[0] == '~') ? expand_tilde(path) : NULL;
                        const char *cp = resolved ? resolved : path;
                        if (stat(cp, &st) == 0)
                            count++;
                        free(resolved);
                    }
                }
            }
        }
        p++;
    }
    return count;
}
