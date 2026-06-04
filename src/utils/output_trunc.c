/*
 * output_trunc.c — Head/tail line-based output truncation.
 *
 * Keeps the first N and last M lines of a large output, inserting
 * an omission marker in the middle. Also supports byte-budget
 * truncation and tool-specific line distribution heuristics.
 */

#include "nevercode.h"

typedef struct {
    const char *start;
    size_t len;
} line_span_t;

static int split_lines(const char *t, size_t tl, line_span_t *sp, int mx) {
    int c = 0;
    const char *p = t, *e = t + tl;
    while (p < e && c < mx) {
        const char *nl = memchr(p, '\n', (size_t)(e - p));
        size_t sl = nl ? (size_t)(nl - p + 1) : (size_t)(e - p);
        sp[c].start = p;
        sp[c].len = sl;
        c++;
        p += sl;
    }
    return c;
}

static int count_lf(const char *t, size_t l) {
    int c = 0;
    for (size_t i = 0; i < l; i++)
        if (t[i] == '\n')
            c++;
    if (l > 0 && t[l - 1] != '\n')
        c++;
    return c;
}

char *output_trunc_lines(const char *t, int hd, int tl) {
    if (!t)
        return NULL;
    size_t l = strlen(t);
    if (l == 0)
        return nc_xstrdup("");
    if (hd <= 0)
        hd = OT_DEFAULT_HEAD_LINES;
    if (tl <= 0)
        tl = OT_DEFAULT_TAIL_LINES;

    int tot = count_lf(t, l);
    if (tot <= hd + tl)
        return nc_xstrdup(t);

    line_span_t *sp = calloc((size_t)(tot + 1), sizeof(line_span_t));
    if (!sp)
        return nc_xstrdup(t);
    int n = split_lines(t, l, sp, tot + 1);

    strbuf_t sb;
    strbuf_init(&sb);

    for (int i = 0; i < hd && i < n; i++)
        strbuf_append(&sb, sp[i].start, sp[i].len);

    char mk[NC_TAG_MAX];
    snprintf(mk, sizeof(mk), "\n... %d lines omitted (total %d lines) ...\n\n", n - hd - tl, n);
    strbuf_append_str(&sb, mk);

    int ts = n - tl;
    if (ts < hd)
        ts = hd;
    for (int i = ts; i < n; i++)
        strbuf_append(&sb, sp[i].start, sp[i].len);

    free(sp);
    return strbuf_detach(&sb);
}

char *output_trunc_smart(const char *t, size_t mb, int hd, int tl) {
    if (!t)
        return NULL;
    if (strlen(t) <= mb)
        return nc_xstrdup(t);

    char *lt = output_trunc_lines(t, hd, tl);
    if (!lt)
        return nc_xstrdup(t);
    if (strlen(lt) <= mb)
        return lt;

    char *bt = middle_truncate_bytes(lt, mb);
    free(lt);
    return bt;
}

char *output_trunc_tool_result(const char *t, const char *tn, size_t mb) {
    if (!t)
        return NULL;
    if (strlen(t) <= mb)
        return nc_xstrdup(t);

    int h = OT_DEFAULT_HEAD_LINES, tl = OT_DEFAULT_TAIL_LINES;
    if (tn) {
        if (strstr(tn, "grep") || strstr(tn, "search"))
            tl = OT_SEARCH_TAIL_LINES;
        else if (strstr(tn, "bash") || strstr(tn, "shell")) {
            h = OT_SHELL_HEAD_LINES;
            tl = OT_SHELL_TAIL_LINES;
        }
    }
    return output_trunc_smart(t, mb, h, tl);
}

int output_trunc_count_lines(const char *t) {
    if (!t)
        return 0;
    return count_lf(t, strlen(t));
}
