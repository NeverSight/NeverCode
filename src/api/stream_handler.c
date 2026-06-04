#include "repl.h"
#include "terminal.h"

static void record_ttft(stream_state_t *st) {
    if (st->ttft_ms >= 0)
        return;
    st->ttft_ms = nc_mono_ms() - st->request_start_ms;
}

void stream_state_init(stream_state_t *s) {
    memset(s, 0, sizeof(*s));
    strbuf_init(&s->text);
    s->ttft_ms = -1;
    s->request_start_ms = nc_mono_ms();
}

void stream_state_free(stream_state_t *s) {
    strbuf_free(&s->text);
    for (int i = 0; i < s->num_tc; i++)
        strbuf_free(&s->pending_tc[i].args);
}

void on_stream_event(const stream_event_t *ev, void *ud) {
    stream_state_t *st = ud;

    switch (ev->type) {
    case STREAM_EVENT_THINKING_DELTA:
        if (g_cfg_ptr && g_cfg_ptr->verbose && ev->text) {
            term_stream_clear_waiting();
            fprintf(stdout, T_DIM "%s" T_RESET, ev->text);
            fflush(stdout);
        }
        break;

    case STREAM_EVENT_TEXT_DELTA:
        record_ttft(st);
        term_stream_text(ev->text);
        strbuf_append_str(&st->text, ev->text);
        event_dispatch_text(st->turn_num, ev->text);
        break;

    case STREAM_EVENT_TOOL_USE_BEGIN: {
        record_ttft(st);
        term_stream_clear_waiting();
        int idx = st->num_tc;
        if (idx >= NEVERCODE_MAX_TOOL_CALLS)
            break;
        st->num_tc++;
        if (ev->tool_use_id)
            nc_strlcpy(st->pending_tc[idx].id, ev->tool_use_id, sizeof(st->pending_tc[idx].id));
        if (ev->tool_name)
            nc_strlcpy(st->pending_tc[idx].name, ev->tool_name, sizeof(st->pending_tc[idx].name));
        strbuf_init(&st->pending_tc[idx].args);
        streaming_exec_tool_begin(streaming_exec_global(), ev->tool_use_id, ev->tool_name);
        event_dispatch_tool(st->turn_num, ev->tool_name, false);
        break;
    }

    case STREAM_EVENT_TOOL_USE_DELTA:
        if (st->num_tc > 0 && ev->json_fragment) {
            strbuf_append_str(&st->pending_tc[st->num_tc - 1].args, ev->json_fragment);
            streaming_exec_tool_delta(streaming_exec_global(), ev->json_fragment);
        }
        break;

    case STREAM_EVENT_TOOL_USE_END:
        streaming_exec_tool_end(streaming_exec_global());
        if (st->num_tc > 0)
            event_dispatch_tool(st->turn_num, st->pending_tc[st->num_tc - 1].name, true);
        break;

    case STREAM_EVENT_MESSAGE_STOP:
        st->stop_reason = ev->stop_reason;
        break;

    case STREAM_EVENT_USAGE:
        if (ev->input_tokens)
            st->input_tokens = ev->input_tokens;
        if (ev->output_tokens)
            st->output_tokens = ev->output_tokens;
        st->cache_creation_tokens += ev->cache_creation_tokens;
        st->cache_read_tokens += ev->cache_read_tokens;
        break;

    case STREAM_EVENT_ERROR:
        term_stream_clear_waiting();
        st->had_error = true;
        if (ev->error_message)
            nc_strlcpy(st->errmsg, ev->error_message, sizeof(st->errmsg));
        event_dispatch_error(st->turn_num, ev->error_message);
        break;
    }
}
