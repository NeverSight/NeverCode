/*
 * event_bus.c — lightweight in-process event fan-out
 */

#include "nevercode.h"

typedef struct {
    bool used;
    event_type_t type;
    event_handler_fn fn;
    void *ctx;
    int id;
} sub_t;

static struct {
    sub_t subs[MAX_EVENT_SUBS];
    int next_id;
    int nsubs;
    int emit_tot;
    int deliver_tot;
} g;

void event_bus_init(void) {
    memset(&g, 0, sizeof g);
    g.next_id = 1;
}

void event_bus_free(void) {
    memset(&g, 0, sizeof g);
}

int event_bus_subscribe(event_type_t type, event_handler_fn handler, void *ctx) {
    if (!handler)
        return -1;
    for (int i = 0; i < MAX_EVENT_SUBS; i++) {
        if (!g.subs[i].used) {
            g.subs[i].used = true;
            g.subs[i].type = type;
            g.subs[i].fn = handler;
            g.subs[i].ctx = ctx;
            g.subs[i].id = g.next_id++;
            g.nsubs++;
            return g.subs[i].id;
        }
    }
    return -1;
}

int event_bus_unsubscribe(int sub_id) {
    for (int i = 0; i < MAX_EVENT_SUBS; i++) {
        if (g.subs[i].used && g.subs[i].id == sub_id) {
            g.subs[i].used = false;
            g.nsubs--;
            return 0;
        }
    }
    return -1;
}

int event_bus_emit(const event_t *event) {
    if (!event)
        return -1;
    g.emit_tot++;
    int n = 0;
    for (int i = 0; i < MAX_EVENT_SUBS; i++) {
        if (!g.subs[i].used)
            continue;
        if (g.subs[i].type != event->type)
            continue;
        g.subs[i].fn(event, g.subs[i].ctx);
        n++;
        g.deliver_tot++;
    }
    return n;
}

int event_bus_emit_simple(event_type_t type, const char *detail) {
    event_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.detail = detail;
    e.timestamp = nc_wall_sec();
    return event_bus_emit(&e);
}

int event_bus_emit_tool(event_type_t type, const char *tool_name, const char *detail, int score) {
    event_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.tool_name = tool_name;
    e.detail = detail;
    e.score = score;
    e.timestamp = nc_wall_sec();
    return event_bus_emit(&e);
}

char *event_bus_format_stats(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "EventBus: subscribers=%d emitted=%d delivered=%d\n", g.nsubs, g.emit_tot,
                   g.deliver_tot);
    return strbuf_detach(&sb);
}
