/*
 * protocol.c — Structured event and message protocol types.
 *
 * Defines the wire-level event types exchanged between the agent core
 * and external consumers (CLI, server mode, bridge). Provides
 * serialization helpers for JSON-based event streams.
 */
#include "nevercode.h"
#include "defaults.h"

/* PROTO_MAX_EVENTS, PROTO_ID_LEN, PROTO_DATA_LEN in defaults.h */
#define PROTO_ID_MAX PROTO_ID_LEN
#define PROTO_DATA_MAX PROTO_DATA_LEN

typedef enum {
    PEVT_TURN_START = 0,
    PEVT_TURN_END,
    PEVT_TEXT_CHUNK,
    PEVT_THINKING_CHUNK,
    PEVT_TOOL_CALL,
    PEVT_TOOL_RESULT,
    PEVT_APPROVAL_REQ,
    PEVT_APPROVAL_RESP,
    PEVT_ERROR,
    PEVT_WARNING,
    PEVT_SESSION_START,
    PEVT_SESSION_END,
    PEVT_COMPACT,
    PEVT_CONTEXT_SWITCH,
    PEVT_AGENT_SPAWN,
    PEVT_AGENT_DONE,
    PEVT_CUSTOM,
} pevt_type_t;

typedef struct {
    pevt_type_t type;
    char id[PROTO_ID_MAX];
    char data[PROTO_DATA_MAX];
    int data_len;
    int turn_id;
    time_t timestamp;
    bool consumed;
} pevt_event_t;

static struct {
    pevt_event_t events[PROTO_MAX_EVENTS];
    int head;
    int tail;
    int count;
    int next_id;
} g_proto;

void proto_init(void) {
    memset(&g_proto, 0, sizeof(g_proto));
    g_proto.next_id = 1;
}

static int ring_push(pevt_event_t *ev) {
    if (g_proto.count >= PROTO_MAX_EVENTS) {
        g_proto.head = (g_proto.head + 1) % PROTO_MAX_EVENTS;
        g_proto.count--;
    }
    g_proto.events[g_proto.tail] = *ev;
    g_proto.tail = (g_proto.tail + 1) % PROTO_MAX_EVENTS;
    g_proto.count++;
    return 0;
}

int proto_emit(pevt_type_t type, int turn_id, const char *data, int data_len) {
    pevt_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.turn_id = turn_id;
    ev.timestamp = nc_time_now();
    snprintf(ev.id, PROTO_ID_MAX, "evt_%d", g_proto.next_id++);
    if (data && data_len > 0) {
        int cp = data_len < PROTO_DATA_MAX - 1 ? data_len : PROTO_DATA_MAX - 1;
        memcpy(ev.data, data, cp);
        ev.data_len = cp;
    }
    return ring_push(&ev);
}

int proto_emit_str(pevt_type_t type, int turn_id, const char *msg) {
    return proto_emit(type, turn_id, msg, msg ? (int)strlen(msg) : 0);
}

int proto_pending(void) {
    return g_proto.count;
}

const pevt_event_t *proto_peek(void) {
    if (g_proto.count == 0)
        return NULL;
    return &g_proto.events[g_proto.head];
}

int proto_consume(pevt_event_t *out) {
    if (g_proto.count == 0)
        return -1;
    if (out)
        *out = g_proto.events[g_proto.head];
    g_proto.head = (g_proto.head + 1) % PROTO_MAX_EVENTS;
    g_proto.count--;
    return 0;
}

int proto_drain(pevt_event_t *out, int cap) {
    int n = 0;
    while (n < cap && g_proto.count > 0) {
        if (out)
            out[n] = g_proto.events[g_proto.head];
        g_proto.head = (g_proto.head + 1) % PROTO_MAX_EVENTS;
        g_proto.count--;
        n++;
    }
    return n;
}

const char *proto_type_name(pevt_type_t t) {
    static const char *names[] = {"turn_start", "turn_end",       "text_chunk",    "thinking_chunk",
                                  "tool_call",  "tool_result",    "approval_req",  "approval_resp",
                                  "error",      "warning",        "session_start", "session_end",
                                  "compact",    "context_switch", "agent_spawn",   "agent_done",
                                  "custom"};
    return (t >= 0 && t <= PEVT_CUSTOM) ? names[t] : "unknown";
}

int proto_format_json(const pevt_event_t *ev, char *out, int cap) {
    return snprintf(
        out, cap, "{\"id\":\"%s\",\"type\":\"%s\",\"turn\":%d,\"ts\":%ld,\"data_len\":%d}", ev->id,
        proto_type_name(ev->type), ev->turn_id, (long)ev->timestamp, ev->data_len);
}

void proto_free(void) {
    memset(&g_proto, 0, sizeof(g_proto));
}
