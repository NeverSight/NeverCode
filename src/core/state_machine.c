/*
 * state_machine.c — session and turn phase tracking
 */

#include "nevercode.h"

static struct {
    char *sid;
    session_phase_t sp;
    turn_phase_t tp;
    int turn_no;
    int turn_id_ctr;
    int tid;
    long ts0;
    long last_act;
    int tool_cnt;
    int err_mark;
    int ncb;
    state_transition_cb_t cbs[MAX_STATE_CALLBACKS];
    void *cctx[MAX_STATE_CALLBACKS];
} g;

#define mono_ms nc_mono_ms

static void fire(session_phase_t from, session_phase_t to) {
    for (int i = 0; i < g.ncb; i++)
        if (g.cbs[i])
            g.cbs[i](from, to, g.cctx[i]);
}

static int sess_go(session_phase_t to) {
    session_phase_t from = g.sp;
    int ok = 0;
    switch (from) {
    case SESSION_IDLE:
        ok = (to == SESSION_INITIALIZING);
        break;
    case SESSION_INITIALIZING:
        ok = (to == SESSION_READY);
        break;
    case SESSION_READY:
        ok = (to == SESSION_RUNNING || to == SESSION_SHUTTING_DOWN);
        break;
    case SESSION_RUNNING:
        ok = (to == SESSION_COMPACTING || to == SESSION_SUSPENDED || to == SESSION_SHUTTING_DOWN);
        break;
    case SESSION_COMPACTING:
        ok = (to == SESSION_RUNNING);
        break;
    case SESSION_SUSPENDED:
        ok = (to == SESSION_RUNNING);
        break;
    case SESSION_SHUTTING_DOWN:
        ok = (to == SESSION_TERMINATED);
        break;
    case SESSION_TERMINATED:
        ok = 0;
        break;
    default:
        ok = 0;
    }
    if (!ok)
        return -1;
    g.sp = to;
    g.last_act = mono_ms();
    fire(from, to);
    return 0;
}

static int turn_go(turn_phase_t to) {
    turn_phase_t from = g.tp;
    int ok = 0;
    switch (from) {
    case TURN_IDLE:
    case TURN_COMPLETE:
        ok = (to == TURN_PREPARING);
        break;
    case TURN_PREPARING:
        ok = (to == TURN_API_CALL);
        break;
    case TURN_API_CALL:
        ok = (to == TURN_STREAMING);
        break;
    case TURN_STREAMING:
        ok = (to == TURN_TOOL_ROUTING);
        break;
    case TURN_TOOL_ROUTING:
        ok = (to == TURN_TOOL_EXECUTING);
        break;
    case TURN_TOOL_EXECUTING:
        ok = (to == TURN_RESULT_COLLECTING);
        break;
    case TURN_RESULT_COLLECTING:
        ok = (to == TURN_POST_PROCESSING);
        break;
    case TURN_POST_PROCESSING:
        ok = (to == TURN_COMPLETE);
        break;
    default:
        ok = 0;
    }
    if (!ok)
        return -1;
    g.tp = to;
    g.last_act = mono_ms();
    return 0;
}

void state_machine_init(const char *session_id) {
    state_machine_free();
    if (session_id && session_id[0])
        g.sid = nc_xstrdup(session_id);
    else
        g.sid = nc_xstrdup("default");
    g.sp = SESSION_IDLE;
    g.tp = TURN_IDLE;
    g.turn_no = 0;
    g.turn_id_ctr = 0;
    g.tid = 0;
    g.ts0 = mono_ms();
    g.last_act = g.ts0;
    g.tool_cnt = 0;
    g.err_mark = 0;
    g.ncb = 0;
}

void state_machine_free(void) {
    if (g.sid)
        free(g.sid);
    memset(&g, 0, sizeof g);
}

int state_add_transition_cb(state_transition_cb_t cb, void *ctx) {
    if (!cb || g.ncb >= MAX_STATE_CALLBACKS)
        return -1;
    g.cbs[g.ncb] = cb;
    g.cctx[g.ncb] = ctx;
    g.ncb++;
    return g.ncb - 1;
}

int state_session_begin(void) {
    return sess_go(SESSION_INITIALIZING);
}

int state_session_ready(void) {
    return sess_go(SESSION_READY);
}

int state_session_run(void) {
    if (g.sp == SESSION_READY || g.sp == SESSION_COMPACTING || g.sp == SESSION_SUSPENDED)
        return sess_go(SESSION_RUNNING);
    return -1;
}

int state_session_compact(void) {
    if (g.sp != SESSION_RUNNING)
        return -1;
    return sess_go(SESSION_COMPACTING);
}

int state_session_suspend(void) {
    if (g.sp != SESSION_RUNNING)
        return -1;
    return sess_go(SESSION_SUSPENDED);
}

int state_session_resume(void) {
    if (g.sp != SESSION_SUSPENDED)
        return -1;
    return sess_go(SESSION_RUNNING);
}

int state_session_shutdown(void) {
    if (g.sp == SESSION_TERMINATED || g.sp == SESSION_IDLE)
        return -1;
    return sess_go(SESSION_SHUTTING_DOWN);
}

int state_session_terminate(void) {
    if (g.sp != SESSION_SHUTTING_DOWN)
        return -1;
    return sess_go(SESSION_TERMINATED);
}

session_phase_t state_session_phase(void) {
    return g.sp;
}

const char *state_session_phase_name(session_phase_t p) {
    switch (p) {
    case SESSION_IDLE:
        return "idle";
    case SESSION_INITIALIZING:
        return "initializing";
    case SESSION_READY:
        return "ready";
    case SESSION_RUNNING:
        return "running";
    case SESSION_COMPACTING:
        return "compacting";
    case SESSION_SUSPENDED:
        return "suspended";
    case SESSION_SHUTTING_DOWN:
        return "shutting_down";
    case SESSION_TERMINATED:
        return "terminated";
    default:
        return "?";
    }
}

int state_session_turn_number(void) {
    return g.turn_no;
}

const char *state_session_id(void) {
    return g.sid ? g.sid : "";
}

long state_session_uptime_ms(void) {
    return mono_ms() - g.ts0;
}

long state_session_idle_ms(void) {
    return mono_ms() - g.last_act;
}

int state_turn_begin(void) {
    if (g.tp != TURN_IDLE && g.tp != TURN_COMPLETE)
        return -1;
    g.tid = ++g.turn_id_ctr;
    g.tool_cnt = 0;
    g.err_mark = 0;
    g.turn_no++;
    return turn_go(TURN_PREPARING);
}

int state_turn_api_call(void) {
    return turn_go(TURN_API_CALL);
}

int state_turn_streaming(void) {
    return turn_go(TURN_STREAMING);
}

int state_turn_tool_route(void) {
    return turn_go(TURN_TOOL_ROUTING);
}

int state_turn_tool_exec(void) {
    int r = turn_go(TURN_TOOL_EXECUTING);
    if (r == 0)
        g.tool_cnt++;
    return r;
}

int state_turn_result_collect(void) {
    return turn_go(TURN_RESULT_COLLECTING);
}

int state_turn_post_process(void) {
    return turn_go(TURN_POST_PROCESSING);
}

int state_turn_complete(void) {
    return turn_go(TURN_COMPLETE);
}

const char *state_turn_phase_name(turn_phase_t p) {
    switch (p) {
    case TURN_IDLE:
        return "idle";
    case TURN_PREPARING:
        return "preparing";
    case TURN_API_CALL:
        return "api_call";
    case TURN_STREAMING:
        return "streaming";
    case TURN_TOOL_ROUTING:
        return "tool_routing";
    case TURN_TOOL_EXECUTING:
        return "tool_executing";
    case TURN_RESULT_COLLECTING:
        return "result_collecting";
    case TURN_POST_PROCESSING:
        return "post_processing";
    case TURN_COMPLETE:
        return "complete";
    default:
        return "?";
    }
}

int state_turn_id(void) {
    return g.tid;
}

int state_turn_tool_count(void) {
    return g.tool_cnt;
}

char *state_format_status(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "session=%s phase=%s turn#=%d turn_phase=%s tools=%d err=%d\n",
                   state_session_id(), state_session_phase_name(g.sp), g.turn_no,
                   state_turn_phase_name(g.tp), g.tool_cnt, g.err_mark);
    return strbuf_detach(&sb);
}
