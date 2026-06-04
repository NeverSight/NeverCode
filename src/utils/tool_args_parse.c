/*
 * tool_args_parse.c — Lenient JSON parse for streamed tool arguments.
 *
 * Small models and some gateways emit malformed JSON (markdown fences,
 * trailing commas, bare keys).
 */

#include "nevercode.h"

static char *trim_dup(const char *s, size_t n) {
    if (!s)
        return NULL;
    size_t a = 0, b = n;
    while (a < b && isspace((unsigned char)s[a]))
        a++;
    while (b > a && isspace((unsigned char)s[b - 1]))
        b--;
    size_t len = b - a;
    char *out = malloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, s + a, len);
    out[len] = '\0';
    return out;
}

/* Remove trailing commas before } or ] (string-aware). */
static void strip_trailing_commas(char *s) {
    bool in_str = false;
    bool esc = false;
    for (char *p = s; *p;) {
        if (esc) {
            esc = false;
            p++;
            continue;
        }
        if (in_str) {
            if (*p == '\\')
                esc = true;
            else if (*p == '"')
                in_str = false;
            p++;
            continue;
        }
        if (*p == '"') {
            in_str = true;
            p++;
            continue;
        }
        if (*p == ',' && (p[1] == '}' || p[1] == ']')) {
            memmove(p, p + 1, strlen(p));
            continue;
        }
        p++;
    }
}

static char *unwrap_markdown_fence(const char *s) {
    if (!s)
        return NULL;
    const char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (!nc_starts_with(p, "```"))
        return NULL;
    p += 3;
    if (nc_starts_with(p, "json"))
        p += 4;
    while (*p && *p != '\n' && *p != '\r')
        p++;
    while (*p == '\n' || *p == '\r')
        p++;

    const char *body = p;
    const char *end = strstr(body, "```");
    if (!end)
        return nc_xstrdup(body);
    return trim_dup(body, (size_t)(end - body));
}

static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* [{,]\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*:  →  quoted key */
static char *quote_bare_keys(const char *s) {
    size_t n = strlen(s);
    char *out = malloc(n * 2 + 4);
    if (!out)
        return NULL;
    size_t o = 0;
    bool in_str = false;
    bool esc = false;

    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)s[i];
        if (esc) {
            out[o++] = (char)c;
            esc = false;
            i++;
            continue;
        }
        if (in_str) {
            if (c == '\\')
                esc = true;
            else if (c == '"')
                in_str = false;
            out[o++] = (char)c;
            i++;
            continue;
        }
        if (c == '"') {
            in_str = true;
            out[o++] = '"';
            i++;
            continue;
        }
        if (c == '{' || c == ',') {
            out[o++] = (char)c;
            i++;
            size_t sp = i;
            while (i < n && isspace((unsigned char)s[i]))
                i++;
            size_t id0 = i;
            while (i < n && is_ident_char(s[i]))
                i++;
            size_t id1 = i;
            while (i < n && isspace((unsigned char)s[i]))
                i++;
            if (id1 > id0 && i < n && s[i] == ':' &&
                (isalpha((unsigned char)s[id0]) || s[id0] == '_')) {
                for (size_t k = sp; k < id0; k++)
                    out[o++] = s[k];
                out[o++] = '"';
                memcpy(out + o, s + id0, id1 - id0);
                o += id1 - id0;
                out[o++] = '"';
                out[o++] = ':';
                i++;
                continue;
            }
            /* Not a bare-key site — copy whitespace + token we scanned */
            for (size_t k = sp; k < id1; k++)
                out[o++] = s[k];
            i = id1;
            continue;
        }
        out[o++] = (char)c;
        i++;
    }
    out[o] = '\0';
    return out;
}

cJSON *tool_args_parse_json(const char *args_str) {
    if (!args_str || !args_str[0])
        return NULL;

    cJSON *j = cJSON_Parse(args_str);
    if (j)
        return j;

    char *fence = unwrap_markdown_fence(args_str);
    const char *candidate = fence ? fence : args_str;
    char *work = trim_dup(candidate, strlen(candidate));
    free(fence);
    if (!work)
        return NULL;

    strip_trailing_commas(work);
    j = cJSON_Parse(work);
    if (j) {
        free(work);
        return j;
    }

    char *q = quote_bare_keys(work);
    free(work);
    if (!q)
        return NULL;
    strip_trailing_commas(q);
    j = cJSON_Parse(q);
    free(q);
    return j;
}
