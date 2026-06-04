/*
 * key_hint.c — contextual keyboard shortcut hints
 *
 * Renders a compact bar of relevant shortcuts based on
 * current UI state (idle, streaming, plan mode, etc.).
 */

#include "nevercode.h"
#include "defaults.h"
#include "terminal.h"

typedef struct {
    const char *key;
    const char *desc;
} hint_entry_t;

static const hint_entry_t IDLE_HINTS[] = {{"Enter", "send"},
                                          {"Ctrl+C", "interrupt"},
                                          {"/help", "commands"},
                                          {"Ctrl+D", "quit"},
                                          {NULL, NULL}};

static const hint_entry_t STREAMING_HINTS[] = {{"Ctrl+C", "stop"}, {"Esc", "rewind"}, {NULL, NULL}};

static const hint_entry_t PLAN_HINTS[] = {
    {"Enter", "send"}, {"/endplan", "exit plan"}, {"Ctrl+C", "interrupt"}, {NULL, NULL}};

static const hint_entry_t VIM_HINTS[] = {
    {"i", "insert"}, {"Esc", "normal"}, {":q", "quit"}, {":w", "send"}, {NULL, NULL}};

static const hint_entry_t *hint_set_for(int ctx) {
    switch (ctx) {
    case KHINT_CTX_IDLE:
        return IDLE_HINTS;
    case KHINT_CTX_STREAMING:
        return STREAMING_HINTS;
    case KHINT_CTX_PLAN:
        return PLAN_HINTS;
    case KHINT_CTX_VIM:
        return VIM_HINTS;
    default:
        return IDLE_HINTS;
    }
}

char *key_hint_render(int ctx, int max_width) {
    if (max_width <= 0)
        max_width = OSTYLE_WIDTH_DEFAULT;

    const hint_entry_t *set = hint_set_for(ctx);
    strbuf_t sb;
    strbuf_init(&sb);

    int pos = 0;
    for (const hint_entry_t *h = set; h->key; h++) {
        char entry[NC_TAG_MAX];
        int n = snprintf(entry, sizeof(entry), " %s: %s ", h->key, h->desc);
        if (pos + n > max_width && pos > 0)
            break;
        if (pos > 0)
            strbuf_append_str(&sb, KHINT_SEPARATOR);
        strbuf_appendf(&sb, T_REVERSE " %s " T_RESET T_GRAY " %s" T_RESET, h->key, h->desc);
        pos += n;
    }
    if (sb.len == 0)
        strbuf_append_str(&sb, "");
    return strbuf_detach(&sb);
}

char *key_hint_render_plain(int ctx) {
    const hint_entry_t *set = hint_set_for(ctx);
    strbuf_t sb;
    strbuf_init(&sb);
    for (const hint_entry_t *h = set; h->key; h++) {
        if (sb.len > 0)
            strbuf_append_str(&sb, "  ");
        strbuf_appendf(&sb, "%s: %s", h->key, h->desc);
    }
    if (sb.len == 0)
        strbuf_append_str(&sb, "");
    return strbuf_detach(&sb);
}

int key_hint_count(int ctx) {
    const hint_entry_t *set = hint_set_for(ctx);
    int n = 0;
    while (set[n].key)
        n++;
    return n;
}
