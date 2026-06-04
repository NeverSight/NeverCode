/*
 * proc_output_buf.c — Rolling-window output buffer for background processes
 *
 * Fixed-capacity ring buffer that keeps the most recent N bytes of
 * process output. When capacity is exceeded, oldest bytes are silently
 * dropped. snapshot() returns a contiguous copy for consumption.
 */

#include "nevercode.h"
#include "defaults.h"

void proc_outbuf_init(proc_output_buf_t *b, size_t cap) {
    if (!b)
        return;
    memset(b, 0, sizeof(*b));
    if (cap == 0)
        cap = PROC_OUTPUT_BUF_CAPACITY;
    b->data = calloc(1, cap);
    b->capacity = b->data ? cap : 0;
}

void proc_outbuf_free(proc_output_buf_t *b) {
    if (!b)
        return;
    free(b->data);
    memset(b, 0, sizeof(*b));
}

void proc_outbuf_append(proc_output_buf_t *b, const char *data, size_t n) {
    if (!b || !b->data || !data || n == 0)
        return;

    if (n >= b->capacity) {
        memcpy(b->data, data + (n - b->capacity), b->capacity);
        b->head = 0;
        b->len = b->capacity;
        return;
    }

    size_t write_pos = (b->head + b->len) % b->capacity;
    size_t tail_space = b->capacity - write_pos;

    if (b->len + n <= b->capacity) {
        if (n <= tail_space) {
            memcpy(b->data + write_pos, data, n);
        } else {
            memcpy(b->data + write_pos, data, tail_space);
            memcpy(b->data, data + tail_space, n - tail_space);
        }
        b->len += n;
    } else {
        size_t overflow = (b->len + n) - b->capacity;
        if (n <= tail_space) {
            memcpy(b->data + write_pos, data, n);
        } else {
            memcpy(b->data + write_pos, data, tail_space);
            memcpy(b->data, data + tail_space, n - tail_space);
        }
        b->len = b->capacity;
        b->head = (b->head + overflow) % b->capacity;
    }
}

char *proc_outbuf_snapshot(const proc_output_buf_t *b) {
    if (!b || !b->data || b->len == 0)
        return NULL;
    char *out = malloc(b->len + 1);
    if (!out)
        return NULL;

    size_t first = b->capacity - b->head;
    if (first > b->len)
        first = b->len;
    memcpy(out, b->data + b->head, first);
    if (first < b->len)
        memcpy(out + first, b->data, b->len - first);
    out[b->len] = '\0';
    return out;
}

size_t proc_outbuf_len(const proc_output_buf_t *b) {
    return b ? b->len : 0;
}
