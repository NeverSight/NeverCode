#include "nevercode.h"

/* APPROX_BYTES_PER_TOKEN in defaults.h */

int approx_token_count(const char *text) {
    if (!text)
        return 0;
    size_t len = strlen(text);
    return (int)((len + APPROX_BYTES_PER_TOKEN - 1) / APPROX_BYTES_PER_TOKEN);
}

size_t approx_bytes_for_tokens(int tokens) {
    if (tokens <= 0)
        return 0;
    return (size_t)tokens * APPROX_BYTES_PER_TOKEN;
}

static size_t utf8_char_start(const char *s, size_t pos) {
    if (pos == 0)
        return 0;
    while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80)
        pos--;
    return pos;
}

static size_t utf8_next_char(const char *s, size_t pos, size_t len) {
    if (pos >= len)
        return len;
    pos++;
    while (pos < len && ((unsigned char)s[pos] & 0xC0) == 0x80)
        pos++;
    return pos;
}

static int count_lines(const char *s, size_t len) {
    int lines = 0;
    for (size_t i = 0; i < len; i++)
        if (s[i] == '\n')
            lines++;
    return lines + (len > 0 && s[len - 1] != '\n' ? 1 : 0);
}

char *middle_truncate_bytes(const char *s, size_t max_bytes) {
    if (!s)
        return NULL;
    size_t len = strlen(s);
    if (len <= max_bytes)
        return nc_xstrdup(s);

    if (max_bytes == 0) {
        char buf[NC_TAG_MAX];
        snprintf(buf, sizeof(buf), "…%zu chars truncated…", len);
        return nc_xstrdup(buf);
    }

    size_t left_budget = max_bytes / 2;
    size_t right_budget = max_bytes - left_budget;

    size_t left_end = utf8_char_start(s, left_budget);
    if (left_end < left_budget)
        left_end = utf8_next_char(s, left_end, len);
    if (left_end > left_budget)
        left_end = utf8_char_start(s, left_budget);

    size_t right_start_target = len - right_budget;
    size_t right_start = utf8_next_char(s, utf8_char_start(s, right_start_target), len);
    if (right_start < right_start_target)
        right_start = utf8_next_char(s, right_start, len);

    if (right_start <= left_end)
        right_start = left_end;

    size_t removed = right_start - left_end;
    int total_lines = count_lines(s, len);

    char marker[NC_LABEL_MAX];
    snprintf(marker, sizeof(marker), "\n…%zu chars truncated (total %d lines)…\n", removed,
             total_lines);

    size_t mlen = strlen(marker);
    size_t out_len = left_end + mlen + (len - right_start);
    char *out = malloc(out_len + 1);
    if (!out)
        return NULL;

    memcpy(out, s, left_end);
    memcpy(out + left_end, marker, mlen);
    memcpy(out + left_end + mlen, s + right_start, len - right_start);
    out[out_len] = '\0';

    return out;
}

char *middle_truncate_tokens(const char *s, int max_tokens) {
    if (!s)
        return NULL;
    size_t max_bytes = approx_bytes_for_tokens(max_tokens);
    size_t len = strlen(s);
    if (len <= max_bytes)
        return nc_xstrdup(s);

    size_t left_budget = max_bytes / 2;
    size_t right_budget = max_bytes - left_budget;

    size_t left_end = utf8_char_start(s, left_budget);
    size_t right_start = len - right_budget;
    if (right_start > len)
        right_start = 0;
    right_start = utf8_next_char(s, utf8_char_start(s, right_start), len);

    if (right_start <= left_end)
        right_start = left_end;

    size_t removed_bytes = right_start - left_end;
    int removed_tokens =
        (int)((removed_bytes + APPROX_BYTES_PER_TOKEN - 1) / APPROX_BYTES_PER_TOKEN);

    char marker[NC_LABEL_MAX];
    snprintf(marker, sizeof(marker), "\n…%d tokens truncated…\n", removed_tokens);

    size_t mlen = strlen(marker);
    size_t out_len = left_end + mlen + (len - right_start);
    char *out = malloc(out_len + 1);
    if (!out)
        return NULL;

    memcpy(out, s, left_end);
    memcpy(out + left_end, marker, mlen);
    memcpy(out + left_end + mlen, s + right_start, len - right_start);
    out[out_len] = '\0';

    return out;
}

char *tool_result_truncate_middle(const char *output, const char *tool_name, int max_tokens) {
    if (!output)
        return NULL;
    size_t len = strlen(output);
    size_t budget = (max_tokens > 0) ? approx_bytes_for_tokens(max_tokens) : TOOL_RESULT_MAX_CHARS;

    if (len <= budget)
        return nc_xstrdup(output);

    int total_lines = count_lines(output, len);
    char *truncated = (max_tokens > 0) ? middle_truncate_tokens(output, max_tokens)
                                       : middle_truncate_bytes(output, TOOL_RESULT_TRUNCATE_TO);

    if (!truncated)
        return nc_xstrdup(output);

    strbuf_t sb;
    strbuf_init(&sb);

    char header[NC_LABEL_MAX];
    snprintf(header, sizeof(header), "Total output lines: %d\n\n", total_lines);
    strbuf_append_str(&sb, header);
    strbuf_append_str(&sb, truncated);
    free(truncated);

    return strbuf_detach(&sb);
}
