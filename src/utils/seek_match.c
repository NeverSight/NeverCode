/*
 * seek_match.c — Multi-pass fuzzy line matching for apply_patch
 *
 * Finds a contiguous block of pattern lines within a source array
 * using progressively looser comparison: exact, trimmed-right,
 * trimmed-both-sides, and normalized whitespace.
 */

#include "nevercode.h"

static const char *trim_start(const char *s) {
    while (*s && isspace((unsigned char)*s))
        s++;
    return s;
}

static size_t trim_len(const char *s) {
    size_t l = strlen(s);
    while (l > 0 && isspace((unsigned char)s[l - 1]))
        l--;
    return l;
}

static bool cmp_exact(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static bool cmp_rtrim(const char *a, const char *b) {
    size_t la = trim_len(a), lb = trim_len(b);
    return la == lb && memcmp(a, b, la) == 0;
}

static bool cmp_trim(const char *a, const char *b) {
    const char *ta = trim_start(a), *tb = trim_start(b);
    size_t la = trim_len(ta), lb = trim_len(tb);
    return la == lb && memcmp(ta, tb, la) == 0;
}

typedef bool (*cmp_fn)(const char *, const char *);

static int pass(const char **lines, int n, const char **pat, int np, int start, cmp_fn fn) {
    for (int i = start; i <= n - np; i++) {
        bool ok = true;
        for (int j = 0; j < np; j++) {
            if (!fn(lines[i + j], pat[j])) {
                ok = false;
                break;
            }
        }
        if (ok)
            return i;
    }
    return -1;
}

int seek_match(const char **lines, int n, const char **pat, int np, int start, bool eof) {
    if (np == 0)
        return start;
    if (np > n)
        return -1;

    int s = (eof && n >= np) ? n - np : start;
    static const cmp_fn fns[] = {cmp_exact, cmp_rtrim, cmp_trim, cmp_trim};

    for (int p = 0; p < 4; p++) {
        int idx = pass(lines, n, pat, np, s, fns[p]);
        if (idx >= 0)
            return idx;
        if (s != start) {
            idx = pass(lines, n, pat, np, start, fns[p]);
            if (idx >= 0)
                return idx;
        }
    }
    return -1;
}

int seek_match_scored(const char **lines, int n, const char **pat, int np, int start,
                      int *quality) {
    if (np == 0) {
        if (quality)
            *quality = 100;
        return start;
    }
    if (np > n)
        return -1;

    static const cmp_fn fns[] = {cmp_exact, cmp_rtrim, cmp_trim, cmp_trim};
    static const int scores[] = {100, 90, 75, 50};

    for (int p = 0; p < 4; p++) {
        int idx = pass(lines, n, pat, np, start, fns[p]);
        if (idx >= 0) {
            if (quality)
                *quality = scores[p];
            return idx;
        }
    }
    return -1;
}
