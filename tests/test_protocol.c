#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
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
    PEVT_CUSTOM
} pevt_type_t;
typedef struct {
    pevt_type_t type;
    char id[64];
    char data[4096];
    int data_len;
    int turn_id;
    long timestamp;
    bool consumed;
} pevt_event_t;
void proto_init(void);
int proto_emit(pevt_type_t, int, const char *, int);
int proto_emit_str(pevt_type_t, int, const char *);
int proto_pending(void);
const pevt_event_t *proto_peek(void);
int proto_consume(pevt_event_t *);
int proto_drain(pevt_event_t *, int);
const char *proto_type_name(pevt_type_t);
int proto_format_json(const pevt_event_t *, char *, int);
static int tr = 0, tp = 0;
#define R(f)                                                                                       \
    do {                                                                                           \
        tr++;                                                                                      \
        if (f())                                                                                   \
            tp++;                                                                                  \
        else                                                                                       \
            fprintf(stderr, "FAIL:%s\n", #f);                                                      \
    } while (0)
static int pra(void) {
    proto_init();
    assert(proto_pending() == 0);
    return 1;
}
static int prb(void) {
    proto_init();
    proto_emit_str(PEVT_TEXT_CHUNK, 1, "hello");
    assert(proto_pending() == 1);
    const pevt_event_t *e = proto_peek();
    assert(e && e->type == PEVT_TEXT_CHUNK && e->turn_id == 1);
    return 1;
}
static int prc(void) {
    proto_init();
    proto_emit_str(PEVT_TEXT_CHUNK, 1, "a");
    pevt_event_t ev;
    assert(proto_consume(&ev) == 0);
    assert(proto_pending() == 0 && ev.type == PEVT_TEXT_CHUNK);
    return 1;
}
static int prd(void) {
    proto_init();
    for (int i = 0; i < 5; i++)
        proto_emit_str(PEVT_TEXT_CHUNK, i, "x");
    pevt_event_t buf[10];
    int n = proto_drain(buf, 10);
    assert(n == 5 && proto_pending() == 0);
    return 1;
}
static int pre(void) {
    assert(strcmp(proto_type_name(PEVT_TURN_START), "turn_start") == 0);
    assert(strcmp(proto_type_name(PEVT_ERROR), "error") == 0);
    return 1;
}
static int prf(void) {
    proto_init();
    proto_emit_str(PEVT_ERROR, 2, "fail");
    pevt_event_t ev;
    proto_consume(&ev);
    char json[512];
    int n = proto_format_json(&ev, json, 512);
    assert(n > 0 && strstr(json, "error"));
    return 1;
}
static int prg(void) {
    proto_init();
    proto_emit(PEVT_TOOL_CALL, 3, "data", 4);
    const pevt_event_t *e = proto_peek();
    assert(e->data_len == 4 && memcmp(e->data, "data", 4) == 0);
    return 1;
}
int test_protocol_all(void) {
    tr = tp = 0;
    R(pra);
    R(prb);
    R(prc);
    R(prd);
    R(pre);
    R(prf);
    R(prg);
    printf("  protocol: %d/%d passed\n", tp, tr);
    return tr - tp;
}
