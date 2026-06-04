/*
 * diff_render.c — ANSI coloring, stats, and file extraction for unified diffs
 */

#include "nevercode.h"
#include "terminal.h"

typedef struct {
    const char *start;
    size_t len;
} line_ref_t;

static line_ref_t next_line(const char *p) {
    const char *nl = strchr(p, '\n');
    line_ref_t r;
    r.start = p;
    r.len = nl ? (size_t)(nl - p) : strlen(p);
    return r;
}

char *diff_render_colorize(const char *diff_text) {
    if (!diff_text)
        return NULL;

    strbuf_t out;
    strbuf_init(&out);
    const char *p = diff_text;

    while (*p) {
        line_ref_t ln = next_line(p);
        const char *prefix = "";
        const char *suffix = T_RESET;

        if (ln.len >= 4 && memcmp(ln.start, "diff", 4) == 0)
            prefix = T_BOLD;
        else if (ln.len >= 3 && memcmp(ln.start, "---", 3) == 0)
            prefix = T_CYAN;
        else if (ln.len >= 3 && memcmp(ln.start, "+++", 3) == 0)
            prefix = T_CYAN;
        else if (ln.len >= 2 && ln.start[0] == '@' && ln.start[1] == '@')
            prefix = T_CYAN T_DIM;
        else if (ln.len > 0 && ln.start[0] == '+')
            prefix = T_GREEN;
        else if (ln.len > 0 && ln.start[0] == '-')
            prefix = T_RED;
        else
            suffix = "";

        if (prefix[0])
            strbuf_append_str(&out, prefix);
        strbuf_append(&out, ln.start, ln.len);
        if (suffix[0])
            strbuf_append_str(&out, suffix);
        strbuf_append_str(&out, "\n");

        p = ln.start + ln.len;
        if (*p == '\n')
            p++;
    }
    return strbuf_detach(&out);
}

int diff_render_count_changes(const char *diff_text, int *added, int *removed) {
    if (added)
        *added = 0;
    if (removed)
        *removed = 0;
    if (!diff_text)
        return -1;

    const char *p = diff_text;
    while (*p) {
        line_ref_t ln = next_line(p);
        if (ln.len > 0) {
            if (ln.start[0] == '+' && !(ln.len >= 3 && memcmp(ln.start, "+++", 3) == 0)) {
                if (added)
                    (*added)++;
            } else if (ln.start[0] == '-' && !(ln.len >= 3 && memcmp(ln.start, "---", 3) == 0)) {
                if (removed)
                    (*removed)++;
            }
        }
        p = ln.start + ln.len;
        if (*p == '\n')
            p++;
    }
    return 0;
}

char *diff_render_stat(const char *diff_text) {
    int a = 0, r = 0;
    diff_render_count_changes(diff_text, &a, &r);
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "diff: +%d / -%d lines", a, r);
    return strbuf_detach(&sb);
}

int diff_render_extract_files(const char *diff_text, const char **out, int max) {
    if (!diff_text || !out || max <= 0)
        return 0;
    int n = 0;
    const char *p = diff_text;

    while (*p && n < max) {
        line_ref_t ln = next_line(p);
        if (ln.len > 6 && memcmp(ln.start, "+++ b/", 6) == 0) {
            size_t flen = ln.len - 6;
            char *f = malloc(flen + 1);
            if (f) {
                memcpy(f, ln.start + 6, flen);
                f[flen] = '\0';
                out[n++] = f;
            }
        }
        p = ln.start + ln.len;
        if (*p == '\n')
            p++;
    }
    return n;
}

int diff_render_file_count(const char *diff_text) {
    if (!diff_text)
        return 0;
    int n = 0;
    const char *p = diff_text;
    while (*p) {
        line_ref_t ln = next_line(p);
        if (ln.len > 6 && memcmp(ln.start, "+++ b/", 6) == 0)
            n++;
        p = ln.start + ln.len;
        if (*p == '\n')
            p++;
    }
    return n;
}

char *diff_render_summary(const char *diff_text) {
    int a = 0, r = 0;
    diff_render_count_changes(diff_text, &a, &r);
    int fc = diff_render_file_count(diff_text);

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "%d file(s) changed, %d insertion(s), %d deletion(s)", fc, a, r);
    return strbuf_detach(&sb);
}

static bool is_word_sep(char c) {
    return strchr(DIFF_WORD_SEP_CHARS, c) != NULL;
}

typedef struct {
    const char *start;
    size_t len;
} word_token_t;

static int tokenize_line(const char *line, size_t line_len, word_token_t *out, int max) {
    int n = 0;
    size_t i = 0;
    while (i < line_len && n < max) {
        if (is_word_sep(line[i])) {
            out[n].start = line + i;
            out[n].len = 1;
            n++;
            i++;
        } else {
            size_t start = i;
            while (i < line_len && !is_word_sep(line[i]))
                i++;
            out[n].start = line + start;
            out[n].len = i - start;
            n++;
        }
    }
    return n;
}

char *diff_render_word_highlight(const char *old_line, const char *new_line) {
    if (!old_line || !new_line)
        return NULL;

    word_token_t old_toks[DIFF_WORD_MAX_TOKENS], new_toks[DIFF_WORD_MAX_TOKENS];
    int old_n = tokenize_line(old_line, strlen(old_line), old_toks, DIFF_WORD_MAX_TOKENS);
    int new_n = tokenize_line(new_line, strlen(new_line), new_toks, DIFF_WORD_MAX_TOKENS);

    strbuf_t sb;
    strbuf_init(&sb);

    strbuf_append_str(&sb, T_RED "-");
    for (int i = 0; i < old_n; i++) {
        bool found = false;
        for (int j = 0; j < new_n; j++) {
            if (old_toks[i].len == new_toks[j].len &&
                memcmp(old_toks[i].start, new_toks[j].start, old_toks[i].len) == 0) {
                found = true;
                break;
            }
        }
        if (!found)
            strbuf_append_str(&sb, T_BOLD);
        strbuf_append(&sb, old_toks[i].start, old_toks[i].len);
        if (!found)
            strbuf_append_str(&sb, T_RESET T_RED);
    }
    strbuf_append_str(&sb, T_RESET "\n");

    strbuf_append_str(&sb, T_GREEN "+");
    for (int i = 0; i < new_n; i++) {
        bool found = false;
        for (int j = 0; j < old_n; j++) {
            if (new_toks[i].len == old_toks[j].len &&
                memcmp(new_toks[i].start, old_toks[j].start, new_toks[i].len) == 0) {
                found = true;
                break;
            }
        }
        if (!found)
            strbuf_append_str(&sb, T_BOLD);
        strbuf_append(&sb, new_toks[i].start, new_toks[i].len);
        if (!found)
            strbuf_append_str(&sb, T_RESET T_GREEN);
    }
    strbuf_append_str(&sb, T_RESET "\n");

    return strbuf_detach(&sb);
}
