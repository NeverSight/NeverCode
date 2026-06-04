/*
 * review_format.c — Structured code review output formatting
 *
 * Formats code review findings with file locations, line ranges,
 * severity markers, and selection checkboxes.
 */

#include "nevercode.h"

void review_finding_init(review_finding_t *f) {
    memset(f, 0, sizeof(*f));
}

void review_finding_free(review_finding_t *f) {
    if (!f)
        return;
    free(f->title);
    free(f->body);
    free(f->file_path);
    memset(f, 0, sizeof(*f));
}

void review_findings_free(review_finding_t *findings, int count) {
    for (int i = 0; i < count; i++)
        review_finding_free(&findings[i]);
}

int review_finding_set(review_finding_t *f, const char *title, const char *body,
                       const char *file_path, int start_line, int end_line,
                       review_severity_t severity) {
    if (!f || !title)
        return -1;

    f->title = nc_xstrdup(title);
    f->body = body ? nc_xstrdup(body) : NULL;
    f->file_path = file_path ? nc_xstrdup(file_path) : NULL;
    f->start_line = start_line;
    f->end_line = end_line;
    f->severity = severity;
    return 0;
}

static const char *severity_label(review_severity_t s) {
    switch (s) {
    case REVIEW_INFO:
        return "info";
    case REVIEW_WARNING:
        return "warning";
    case REVIEW_ERROR:
        return "error";
    case REVIEW_CRITICAL:
        return "critical";
    default:
        return "info";
    }
}

static const char *severity_icon(review_severity_t s) {
    switch (s) {
    case REVIEW_INFO:
        return "ℹ";
    case REVIEW_WARNING:
        return "⚠";
    case REVIEW_ERROR:
        return "✗";
    case REVIEW_CRITICAL:
        return "🔴";
    default:
        return "ℹ";
    }
}

char *review_format_finding(const review_finding_t *f, int index, const bool *selected) {
    if (!f || !f->title)
        return nc_xstrdup("");

    strbuf_t sb;
    strbuf_init(&sb);

    char line[NC_MSG_MAX];

    if (selected) {
        const char *marker = *selected ? "[x]" : "[ ]";
        snprintf(line, sizeof(line), "- %s ", marker);
    } else {
        snprintf(line, sizeof(line), "- ");
    }
    strbuf_append_str(&sb, line);

    snprintf(line, sizeof(line), "%s [%s] %s", severity_icon(f->severity),
             severity_label(f->severity), f->title);
    strbuf_append_str(&sb, line);

    if (f->file_path) {
        snprintf(line, sizeof(line), " — %s:%d-%d", f->file_path, f->start_line, f->end_line);
        strbuf_append_str(&sb, line);
    }
    strbuf_append_str(&sb, "\n");

    if (f->body && f->body[0]) {
        const char *p = f->body;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t llen = nl ? (size_t)(nl - p) : strlen(p);
            strbuf_append_str(&sb, "  ");
            strbuf_append(&sb, p, llen);
            strbuf_append_str(&sb, "\n");
            p = nl ? nl + 1 : p + llen;
        }
    }

    return strbuf_detach(&sb);
}

char *review_format_findings(const review_finding_t *findings, int count, const bool *selection) {
    if (!findings || count == 0)
        return nc_xstrdup("No review findings.\n");

    strbuf_t sb;
    strbuf_init(&sb);

    const char *header = count > 1 ? "Full review comments:\n\n" : "Review comment:\n\n";
    strbuf_append_str(&sb, header);

    for (int i = 0; i < count; i++) {
        const bool *sel = selection ? &selection[i] : NULL;
        char *item = review_format_finding(&findings[i], i, sel);
        strbuf_append_str(&sb, item);
        free(item);
        if (i < count - 1)
            strbuf_append_str(&sb, "\n");
    }

    return strbuf_detach(&sb);
}

char *review_format_summary(const char *explanation, const review_finding_t *findings, int count) {
    strbuf_t sb;
    strbuf_init(&sb);

    if (explanation && explanation[0]) {
        strbuf_append_str(&sb, explanation);
        if (count > 0)
            strbuf_append_str(&sb, "\n\n");
    }

    if (count > 0) {
        char *block = review_format_findings(findings, count, NULL);
        strbuf_append_str(&sb, block);
        free(block);
    }

    if (sb.len == 0)
        strbuf_append_str(&sb, "Reviewer failed to output a response.\n");

    return strbuf_detach(&sb);
}

int review_count_by_severity(const review_finding_t *findings, int count,
                             review_severity_t severity) {
    int n = 0;
    for (int i = 0; i < count; i++)
        if (findings[i].severity == severity)
            n++;
    return n;
}
