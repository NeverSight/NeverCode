/*
 * event_dispatch.c — Unified event dispatcher
 *
 * Routes session events to appropriate output processors.
 * Supports human-readable, JSONL, and callback-based outputs.
 */

#include "nevercode.h"
#include "defaults.h"

/* ── Internal types ── */

typedef struct {
    dispatch_handler_fn fn;
    void *ctx;
    int event_mask; /* bitmask of dispatch_event_kind_t */
} event_subscriber_t;

typedef struct {
    output_mode_t mode;
    FILE *output_file;
    event_subscriber_t subscribers[EDISP_MAX_SUBSCRIBERS];
    int sub_count;
    bool initialized;
    long event_counter;
} event_dispatch_ctx_t;

static event_dispatch_ctx_t g_dispatch;

/* ── Init / Free ── */

void event_dispatch_init(output_mode_t mode) {
    memset(&g_dispatch, 0, sizeof(g_dispatch));
    g_dispatch.mode = mode;
    g_dispatch.output_file = stdout;
    g_dispatch.initialized = true;
}

void event_dispatch_free(void) {
    g_dispatch.initialized = false;
    g_dispatch.sub_count = 0;
}

/* ── Subscription ── */

int event_dispatch_subscribe(dispatch_handler_fn fn, void *ctx, int event_mask) {
    if (!g_dispatch.initialized || g_dispatch.sub_count >= EDISP_MAX_SUBSCRIBERS)
        return -1;
    event_subscriber_t *s = &g_dispatch.subscribers[g_dispatch.sub_count++];
    s->fn = fn;
    s->ctx = ctx;
    s->event_mask = event_mask;
    return 0;
}

/* ── Dispatch helpers ── */

static const char *event_kind_name(dispatch_event_kind_t k) {
    switch (k) {
    case EV_TURN_START:
        return "turn_start";
    case EV_TURN_COMPLETE:
        return "turn_complete";
    case EV_TURN_ABORT:
        return "turn_abort";
    case EV_TEXT_DELTA:
        return "text_delta";
    case EV_TOOL_START:
        return "tool_start";
    case EV_TOOL_DONE:
        return "tool_done";
    case EV_COMPACT:
        return "compact";
    case EV_ERROR:
        return "error";
    case EV_WARNING:
        return "warning";
    case EV_SESSION_START:
        return "session_start";
    case EV_SESSION_END:
        return "session_end";
    case EV_COST_UPDATE:
        return "cost_update";
    case EV_HANDOFF:
        return "handoff";
    case EV_CUSTOM:
        return "custom";
    }
    return "unknown";
}

static void emit_human(const dispatch_event_t *ev) {
    FILE *f = g_dispatch.output_file;
    switch (ev->kind) {
    case EV_TEXT_DELTA:
        if (ev->text)
            fprintf(f, "%s", ev->text);
        break;
    case EV_TOOL_START:
        fprintf(f, "\n" EV_GLYPH_TOOL_START " %s\n", ev->tool_name ? ev->tool_name : "tool");
        break;
    case EV_TOOL_DONE:
        fprintf(f, EV_GLYPH_TOOL_DONE " %s done\n", ev->tool_name ? ev->tool_name : "tool");
        break;
    case EV_ERROR:
        fprintf(f, "\n" EV_GLYPH_ERROR " Error: %s\n", ev->text ? ev->text : "unknown");
        break;
    case EV_WARNING:
        fprintf(f, EV_GLYPH_WARNING " %s\n", ev->text ? ev->text : "warning");
        break;
    case EV_TURN_COMPLETE:
        fprintf(f, "\n[turn %d done | %d in / %d out | $%.4f]\n", ev->turn, ev->tokens_in,
                ev->tokens_out, ev->cost);
        break;
    case EV_COMPACT:
        fprintf(f, EV_GLYPH_COMPACT " context compacted\n");
        break;
    case EV_HANDOFF:
        fprintf(f, EV_GLYPH_HANDOFF " handoff: %s\n", ev->text ? ev->text : "");
        break;
    default:
        break;
    }
    fflush(f);
}

static void jsonl_escape_str(FILE *f, const char *s) {
    fputc('"', f);
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '"':
            fputs("\\\"", f);
            break;
        case '\\':
            fputs("\\\\", f);
            break;
        case '\n':
            fputs("\\n", f);
            break;
        case '\r':
            fputs("\\r", f);
            break;
        case '\t':
            fputs("\\t", f);
            break;
        default:
            if ((unsigned char)*p < 0x20)
                fprintf(f, "\\u%04x", (unsigned)*p);
            else
                fputc(*p, f);
            break;
        }
    }
    fputc('"', f);
}

static void emit_jsonl(const dispatch_event_t *ev) {
    FILE *f = g_dispatch.output_file;
    fprintf(f, "{\"event\":\"%s\",\"turn\":%d", event_kind_name(ev->kind), ev->turn);
    if (ev->tool_name) {
        fputs(",\"tool\":", f);
        jsonl_escape_str(f, ev->tool_name);
    }
    if (ev->text) {
        fputs(",\"text\":", f);
        jsonl_escape_str(f, ev->text);
    }
    if (ev->tokens_in)
        fprintf(f, ",\"tokens_in\":%d", ev->tokens_in);
    if (ev->tokens_out)
        fprintf(f, ",\"tokens_out\":%d", ev->tokens_out);
    if (ev->cost > 0.0)
        fprintf(f, ",\"cost\":%.6f", ev->cost);
    fprintf(f, ",\"ts\":%ld,\"seq\":%ld}\n", ev->timestamp, g_dispatch.event_counter);
    fflush(f);
}

/* ── Core dispatch ── */

void event_dispatch_emit(const dispatch_event_t *ev) {
    if (!g_dispatch.initialized || !ev)
        return;
    g_dispatch.event_counter++;

    switch (g_dispatch.mode) {
    case OUTPUT_HUMAN:
        emit_human(ev);
        break;
    case OUTPUT_JSONL:
        emit_jsonl(ev);
        break;
    case OUTPUT_QUIET:
        break;
    case OUTPUT_CALLBACK:
        break;
    }

    if ((unsigned)ev->kind < 32) {
        int mask = 1 << ev->kind;
        for (int i = 0; i < g_dispatch.sub_count; i++) {
            if (g_dispatch.subscribers[i].event_mask & mask)
                g_dispatch.subscribers[i].fn(ev, g_dispatch.subscribers[i].ctx);
        }
    }
}

/* ── Convenience emitters ── */

void event_dispatch_text(int turn, const char *text) {
    dispatch_event_t ev = {
        .kind = EV_TEXT_DELTA, .turn = turn, .text = text, .timestamp = nc_wall_sec()};
    event_dispatch_emit(&ev);
}

void event_dispatch_tool(int turn, const char *tool, bool done) {
    dispatch_event_t ev = {.kind = done ? EV_TOOL_DONE : EV_TOOL_START,
                           .turn = turn,
                           .tool_name = tool,
                           .timestamp = nc_wall_sec()};
    event_dispatch_emit(&ev);
}

void event_dispatch_turn_complete(int turn, int tok_in, int tok_out, double cost) {
    dispatch_event_t ev = {.kind = EV_TURN_COMPLETE,
                           .turn = turn,
                           .tokens_in = tok_in,
                           .tokens_out = tok_out,
                           .cost = cost,
                           .timestamp = nc_wall_sec()};
    event_dispatch_emit(&ev);
}

void event_dispatch_error(int turn, const char *msg) {
    dispatch_event_t ev = {.kind = EV_ERROR, .turn = turn, .text = msg, .timestamp = nc_wall_sec()};
    event_dispatch_emit(&ev);
}

output_mode_t event_dispatch_mode(void) {
    return g_dispatch.mode;
}
long event_dispatch_count(void) {
    return g_dispatch.event_counter;
}

const char *output_mode_name(output_mode_t m) {
    switch (m) {
    case OUTPUT_HUMAN:
        return "human";
    case OUTPUT_JSONL:
        return "jsonl";
    case OUTPUT_QUIET:
        return "quiet";
    case OUTPUT_CALLBACK:
        return "callback";
    }
    return "unknown";
}
