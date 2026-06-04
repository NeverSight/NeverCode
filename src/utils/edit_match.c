/*
 * edit_match.c — Shared line-level normalization and progressive fuzzy matching
 *                used by Edit and MultiEdit tools.
 *
 * Six matching levels tried in order:
 *   L1: exact match
 *   L2: right-trim trailing whitespace per line
 *   L3: trim both leading and trailing whitespace per line
 *   L4: collapse runs of whitespace to single space
 *   L5: ignore indentation differences entirely
 *   L6: normalize Unicode (smart quotes, em-dashes, NBSP → ASCII)
 */

#include "nevercode.h"

/* ── Line-level normalization helpers ── */

char *edit_rstrip_lines(const char *s) {
    strbuf_t sb;
    strbuf_init(&sb);
    const char *p = s;
    while (*p) {
        const char *nl = strchr(p, '\n');
        const char *end = nl ? nl : p + strlen(p);
        const char *trim = end;
        while (trim > p && (trim[-1] == ' ' || trim[-1] == '\t'))
            trim--;
        strbuf_append(&sb, p, trim - p);
        if (nl) {
            strbuf_append(&sb, "\n", 1);
            p = nl + 1;
        } else {
            break;
        }
    }
    return strbuf_detach(&sb);
}

char *edit_trim_lines(const char *s) {
    strbuf_t sb;
    strbuf_init(&sb);
    const char *p = s;
    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;
        const char *nl = strchr(p, '\n');
        const char *end = nl ? nl : p + strlen(p);
        const char *trim = end;
        while (trim > p && (trim[-1] == ' ' || trim[-1] == '\t'))
            trim--;
        strbuf_append(&sb, p, trim - p);
        if (nl) {
            strbuf_append(&sb, "\n", 1);
            p = nl + 1;
        } else {
            break;
        }
    }
    return strbuf_detach(&sb);
}

char *edit_collapse_ws(const char *s) {
    strbuf_t sb;
    strbuf_init(&sb);
    bool in_ws = false;
    for (const char *p = s; *p; p++) {
        if (*p == '\n') {
            in_ws = false;
            strbuf_append(&sb, "\n", 1);
        } else if (*p == ' ' || *p == '\t') {
            if (!in_ws) {
                strbuf_append(&sb, " ", 1);
                in_ws = true;
            }
        } else {
            in_ws = false;
            strbuf_append(&sb, p, 1);
        }
    }
    return strbuf_detach(&sb);
}

char *edit_strip_indent(const char *s) {
    strbuf_t sb;
    strbuf_init(&sb);
    const char *p = s;
    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;
        const char *nl = strchr(p, '\n');
        if (nl) {
            strbuf_append(&sb, p, nl - p + 1);
            p = nl + 1;
        } else {
            strbuf_append_str(&sb, p);
            break;
        }
    }
    return strbuf_detach(&sb);
}

char *edit_unicode_norm(const char *s) {
    strbuf_t sb;
    strbuf_init(&sb);
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (p[0] == 0xe2 && p[1] == 0x80) {
            if (p[2] == 0x9c || p[2] == 0x9d) {
                strbuf_append(&sb, "\"", 1);
                p += 3;
                continue;
            }
            if (p[2] == 0x98 || p[2] == 0x99) {
                strbuf_append(&sb, "'", 1);
                p += 3;
                continue;
            }
            if (p[2] == 0x94) {
                strbuf_append(&sb, "--", 2);
                p += 3;
                continue;
            }
            if (p[2] == 0x93) {
                strbuf_append(&sb, "-", 1);
                p += 3;
                continue;
            }
            if (p[2] == 0xa6) {
                strbuf_append(&sb, "...", 3);
                p += 3;
                continue;
            }
        }
        if (p[0] == 0xc2 && p[1] == 0xa0) {
            strbuf_append(&sb, " ", 1);
            p += 2;
            continue;
        }
        strbuf_append(&sb, (const char *)p, 1);
        p++;
    }
    return strbuf_detach(&sb);
}

const edit_norm_fn_t edit_norm_fns[EDIT_FUZZY_MATCH_LEVELS] = {NULL,
                                                               edit_rstrip_lines,
                                                               edit_trim_lines,
                                                               edit_collapse_ws,
                                                               edit_strip_indent,
                                                               edit_unicode_norm};

const char *edit_level_names[EDIT_FUZZY_MATCH_LEVELS] = {
    "exact", "rstrip", "trim", "ws-collapse", "indent-flex", "unicode-norm"};

bool edit_fuzzy_locate(const char *content, const char *old_str, size_t old_len, size_t *out_offset,
                       size_t *out_match_len, int *out_level) {
    const char *hit = strstr(content, old_str);
    if (hit) {
        *out_offset = hit - content;
        *out_match_len = old_len;
        *out_level = 0;
        return true;
    }

    for (int lvl = 1; lvl < EDIT_FUZZY_MATCH_LEVELS; lvl++) {
        edit_norm_fn_t fn = edit_norm_fns[lvl];
        char *n_old = fn(old_str);
        if (!n_old)
            continue;

        int n_old_lines = 1;
        for (const char *c = n_old; *c; c++)
            if (*c == '\n')
                n_old_lines++;

        char **olines = calloc(n_old_lines, sizeof(char *));
        if (!olines) {
            free(n_old);
            continue;
        }
        char *tmp = n_old;
        for (int idx = 0; idx < n_old_lines; idx++) {
            char *nl = strchr(tmp, '\n');
            if (nl) {
                olines[idx] = nc_xstrndup(tmp, nl - tmp);
                tmp = nl + 1;
            } else {
                olines[idx] = nc_xstrdup(tmp);
                break;
            }
        }

        const char *cp = content;
        bool found = false;
        while (*cp) {
            const char *scan = cp;
            int matched = 0;
            for (int j = 0; j < n_old_lines && *scan; j++) {
                const char *nl = strchr(scan, '\n');
                size_t llen = nl ? (size_t)(nl - scan) : strlen(scan);
                char *raw = nc_xstrndup(scan, llen);
                char *norm = fn(raw);
                bool eq = (norm && olines[j] && strcmp(norm, olines[j]) == 0);
                free(raw);
                free(norm);
                if (!eq)
                    break;
                matched++;
                scan = nl ? nl + 1 : scan + llen;
            }
            if (matched == n_old_lines) {
                *out_offset = cp - content;
                *out_match_len = scan - cp;
                *out_level = lvl;
                found = true;
                break;
            }
            const char *next = strchr(cp, '\n');
            if (next)
                cp = next + 1;
            else
                break;
        }

        for (int i = 0; i < n_old_lines; i++)
            free(olines[i]);
        free(olines);
        free(n_old);
        if (found)
            return true;
    }
    return false;
}

int edit_fuzzy_count(const char *content, const char *old_str, size_t old_len, int level) {
    if (level == 0) {
        int count = 0;
        const char *p = content;
        while ((p = strstr(p, old_str)) != NULL) {
            count++;
            p += old_len;
        }
        return count;
    }
    int count = 0;
    size_t off = 0, mlen = 0;
    int lvl = 0;
    const char *p = content;
    while (edit_fuzzy_locate(p, old_str, old_len, &off, &mlen, &lvl)) {
        if (lvl != level)
            break;
        count++;
        p += off + mlen;
    }
    return count;
}
