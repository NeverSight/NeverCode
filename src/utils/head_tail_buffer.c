/*
 * head_tail_buffer.c — Capped output buffer preserving head + tail
 *
 * Keeps the first N/2 bytes (head) and last N/2 bytes (tail), dropping
 * the middle once capacity is exceeded. Tracks omitted bytes for reporting.
 */

#include "nevercode.h"

void htbuf_init(head_tail_buf_t *b, size_t max_bytes) {
    if (!b)
        return;
    memset(b, 0, sizeof(*b));
    b->max_bytes = max_bytes;
    b->head_budget = max_bytes / 2;
    b->tail_budget = max_bytes - b->head_budget;
}

void htbuf_free(head_tail_buf_t *b) {
    if (!b)
        return;
    free(b->head);
    free(b->tail);
    memset(b, 0, sizeof(*b));
}

void htbuf_push(head_tail_buf_t *b, const char *data, size_t len) {
    if (!b || !data || len == 0)
        return;
    if (b->max_bytes == 0) {
        b->omitted_bytes += len;
        return;
    }

    /* Phase 1: Fill head budget */
    if (b->head_len < b->head_budget) {
        size_t room = b->head_budget - b->head_len;
        size_t take = NC_MIN(len, room);
        char *tmp = realloc(b->head, b->head_len + take);
        if (!tmp)
            return;
        b->head = tmp;
        memcpy(b->head + b->head_len, data, take);
        b->head_len += take;
        data += take;
        len -= take;
        if (len == 0)
            return;
    }

    /* Phase 2: Append to tail, evict oldest tail bytes if over budget */
    size_t new_tail_len = b->tail_len + len;
    if (new_tail_len <= b->tail_budget) {
        char *tmp = realloc(b->tail, new_tail_len);
        if (!tmp)
            return;
        b->tail = tmp;
        memcpy(b->tail + b->tail_len, data, len);
        b->tail_len = new_tail_len;
    } else {
        /* Need to trim: keep the newest tail_budget bytes */
        char *merged = malloc(new_tail_len);
        if (!merged)
            return;
        if (b->tail && b->tail_len > 0)
            memcpy(merged, b->tail, b->tail_len);
        memcpy(merged + b->tail_len, data, len);

        size_t drop = new_tail_len - b->tail_budget;
        b->omitted_bytes += drop;

        size_t keep = b->tail_budget;
        free(b->tail);
        b->tail = malloc(keep);
        if (!b->tail) {
            b->tail_len = 0;
            free(merged);
            return;
        }
        memcpy(b->tail, merged + drop, keep);
        b->tail_len = keep;
        free(merged);
    }
}

size_t htbuf_retained(const head_tail_buf_t *b) {
    if (!b)
        return 0;
    return b->head_len + b->tail_len;
}

size_t htbuf_omitted(const head_tail_buf_t *b) {
    if (!b)
        return 0;
    return b->omitted_bytes;
}

char *htbuf_finalize(const head_tail_buf_t *b) {
    if (!b)
        return nc_xstrdup("");
    size_t total = b->head_len + b->tail_len;
    if (total == 0)
        return nc_xstrdup("");

    if (b->omitted_bytes == 0) {
        char *out = malloc(total + 1);
        if (!out)
            return nc_xstrdup("");
        if (b->head && b->head_len > 0)
            memcpy(out, b->head, b->head_len);
        if (b->tail && b->tail_len > 0)
            memcpy(out + b->head_len, b->tail, b->tail_len);
        out[total] = '\0';
        return out;
    }

    char omit_msg[NC_TAG_MAX];
    snprintf(omit_msg, sizeof(omit_msg), "\n\n... [%zu bytes omitted] ...\n\n", b->omitted_bytes);
    size_t msg_len = strlen(omit_msg);
    size_t out_len = b->head_len + msg_len + b->tail_len;
    char *out = malloc(out_len + 1);
    if (!out)
        return nc_xstrdup("");
    size_t off = 0;
    if (b->head && b->head_len > 0) {
        memcpy(out, b->head, b->head_len);
        off += b->head_len;
    }
    memcpy(out + off, omit_msg, msg_len);
    off += msg_len;
    if (b->tail && b->tail_len > 0) {
        memcpy(out + off, b->tail, b->tail_len);
        off += b->tail_len;
    }
    out[off] = '\0';
    return out;
}

char *htbuf_finalize_with_meta(const head_tail_buf_t *b, size_t *out_total, size_t *out_omitted) {
    if (out_total)
        *out_total = b ? (b->head_len + b->tail_len + b->omitted_bytes) : 0;
    if (out_omitted)
        *out_omitted = b ? b->omitted_bytes : 0;
    return htbuf_finalize(b);
}
