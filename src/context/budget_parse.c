/*
 * budget_parse.c — Parse token budget shorthand from user input.
 *
 * Recognises patterns like "+500k", "+2M", "spend 1M tokens",
 * "use 500k tokens" and converts them to an integer token count.
 * Returns 0 when no budget keyword is found.
 */

#include "nevercode.h"
#include <math.h>

static long suffix_multiplier(char c) {
    switch (c) {
    case 'k':
    case 'K':
        return 1000L;
    case 'm':
    case 'M':
        return 1000000L;
    case 'b':
    case 'B':
        return 1000000000L;
    default:
        return 0;
    }
}

static long parse_number_suffix(const char *num_start, int num_len, char suffix) {
    char buf[NC_FMT_MAX];
    if (num_len <= 0 || num_len >= (int)sizeof(buf))
        return 0;
    memcpy(buf, num_start, num_len);
    buf[num_len] = '\0';

    char *end = NULL;
    double val = strtod(buf, &end);
    if (val <= 0.0 || end == buf)
        return 0;

    long mult = suffix_multiplier(suffix);
    if (mult == 0)
        return 0;

    return (long)(val * (double)mult);
}

long budget_parse_shorthand(const char *text) {
    if (!text || !text[0])
        return 0;

    const char *p = text;

    /* Skip leading whitespace */
    while (*p && isspace((unsigned char)*p))
        p++;

    /* Pattern 1: starts with +NNN[kKmMbB] */
    if (*p == '+') {
        p++;
        const char *num_start = p;
        while (*p && (isdigit((unsigned char)*p) || *p == '.'))
            p++;
        int num_len = (int)(p - num_start);
        if (num_len > 0 && suffix_multiplier(*p)) {
            char suf = *p;
            p++;
            if (*p == '\0' || isspace((unsigned char)*p) || *p == '.' || *p == ',' || *p == '!') {
                return parse_number_suffix(num_start, num_len, suf);
            }
        }
    }

    /* Pattern 2: ends with +NNN[kKmMbB] */
    size_t len = strlen(text);
    for (int i = (int)len - 1; i >= 1; i--) {
        if (text[i - 1] == '+' && isdigit((unsigned char)text[i])) {
            const char *ns = text + i;
            const char *ne = ns;
            while (*ne && (isdigit((unsigned char)*ne) || *ne == '.'))
                ne++;
            int nl = (int)(ne - ns);
            if (nl > 0 && suffix_multiplier(*ne)) {
                char sf = *ne;
                const char *after = ne + 1;
                while (*after && isspace((unsigned char)*after))
                    after++;
                if (*after == '\0' || *after == '.' || *after == '!' || *after == '?') {
                    return parse_number_suffix(ns, nl, sf);
                }
            }
            break;
        }
    }

    /* Pattern 3: "use NNN[kKmMbB] tokens" or "spend NNN[kKmMbB] tokens" */
    const char *keywords[] = {"use ", "spend "};
    for (int k = 0; k < 2; k++) {
        const char *kw = keywords[k];
        size_t klen = strlen(kw);
        const char *found = text;
        while ((found = strstr(found, kw)) != NULL) {
            const char *after_kw = found + klen;
            while (*after_kw && isspace((unsigned char)*after_kw))
                after_kw++;
            const char *num_s = after_kw;
            while (*after_kw && (isdigit((unsigned char)*after_kw) || *after_kw == '.'))
                after_kw++;
            int nlen = (int)(after_kw - num_s);
            if (nlen > 0 && suffix_multiplier(*after_kw)) {
                char sf = *after_kw;
                after_kw++;
                while (*after_kw && isspace((unsigned char)*after_kw))
                    after_kw++;
                if (nc_starts_with(after_kw, "token")) {
                    return parse_number_suffix(num_s, nlen, sf);
                }
            }
            found++;
        }
    }

    return 0;
}
