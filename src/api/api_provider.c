#include "api.h"
#include "api_messages.h"
#include "api_compat.h"
#include "terminal.h"

typedef struct {
    stream_callback_t real_cb;
    void *real_ud;
    bool got_overloaded;
    long http_status;
} retry_wrapper_t;

static void retry_cb(const stream_event_t *ev, void *ud) {
    retry_wrapper_t *rw = ud;
    if (ev->type == STREAM_EVENT_ERROR && ev->error_message) {
        if (strstr(ev->error_message, HTTP_ERR_OVERLOADED) ||
            strstr(ev->error_message, HTTP_ERR_RATE_LIMIT) ||
            strstr(ev->error_message, HTTP_STR_429) || strstr(ev->error_message, HTTP_STR_503) ||
            strstr(ev->error_message, HTTP_STR_529)) {
            rw->got_overloaded = true;
            return;
        }
    }
    rw->real_cb(ev, rw->real_ud);
}

static int do_stream(const message_t *msgs, int n, const char *sys, const config_t *cfg,
                     const tool_def_t *tools, int nt, stream_callback_t cb, void *ud) {
    switch (cfg->provider) {
    case PROVIDER_NATIVE:
        return api_messages_stream(msgs, n, sys, cfg, tools, nt, cb, ud);
    case PROVIDER_COMPAT:
        return api_compat_stream(msgs, n, sys, cfg, tools, nt, cb, ud);
    }
    return -1;
}

void api_stream_record_cache(const config_t *cfg, const tool_def_t *tools, int num_tools) {
    if (!cfg)
        return;
    const char *m = config_model(cfg);
    if (m && m[0])
        cache_hint_set_model_hash(m);
    cache_hint_set_tools_hash(tools, num_tools);
    cache_hint_tick();
}

int api_stream_request(const message_t *messages, int num_messages, const char *system_prompt,
                       const config_t *cfg, const tool_def_t *tools, int num_tools,
                       stream_callback_t callback, void *userdata) {
    int max_retries = RETRY_PROVIDER_MAX_ATTEMPTS;

    for (int attempt = 0; attempt <= max_retries; attempt++) {
        retry_wrapper_t rw = {.real_cb = callback, .real_ud = userdata, .got_overloaded = false};

        int rc = do_stream(messages, num_messages, system_prompt, cfg, tools, num_tools,
                           attempt < max_retries ? retry_cb : callback,
                           attempt < max_retries ? &rw : userdata);

        if (rc == 0 || g_interrupted)
            return rc;

        if (!rw.got_overloaded || attempt >= max_retries)
            return rc;

        int delay_ms = nc_backoff_exp2_sat(RETRY_PROVIDER_FALLBACK_MS, RETRY_MAX_DELAY_MS, attempt);
        term_color(T_DIM, " [retrying in %dms (%d/%d)...]\n", delay_ms, attempt + 1, max_retries);
        usleep(delay_ms * 1000);
    }
    return -1;
}

/* ---- Simple one-shot API call ---- */

typedef struct {
    strbuf_t *response;
    int in_tokens;
    int out_tokens;
    bool got_error;
} simple_ctx_t;

static void simple_cb(const stream_event_t *ev, void *ud) {
    simple_ctx_t *ctx = ud;
    switch (ev->type) {
    case STREAM_EVENT_TEXT_DELTA:
        if (ev->text)
            strbuf_append_str(ctx->response, ev->text);
        break;
    case STREAM_EVENT_USAGE:
        ctx->in_tokens += ev->input_tokens;
        ctx->out_tokens += ev->output_tokens;
        break;
    case STREAM_EVENT_ERROR:
        ctx->got_error = true;
        break;
    default:
        break;
    }
}

int api_stream_simple(const char *base_url, const char *api_key, const char *model,
                      provider_type_t provider, const char *system_prompt,
                      const message_t *messages, int num_messages, strbuf_t *response_out,
                      int *in_tokens_out, int *out_tokens_out) {
    config_t tmp_cfg;
    memset(&tmp_cfg, 0, sizeof(tmp_cfg));
    tmp_cfg.provider = provider;
    tmp_cfg.model = (char *)model;
    tmp_cfg.max_tokens = COMPACT_INTERNAL_MAX_TOKENS;

    if (provider == PROVIDER_COMPAT) {
        tmp_cfg.compat_base_url = (char *)base_url;
        tmp_cfg.compat_api_key = (char *)api_key;
    } else {
        tmp_cfg.native_base_url = (char *)base_url;
        tmp_cfg.native_api_key = (char *)api_key;
    }

    simple_ctx_t ctx = {
        .response = response_out, .in_tokens = 0, .out_tokens = 0, .got_error = false};

    int rc = do_stream(messages, num_messages, system_prompt, &tmp_cfg, NULL, 0, simple_cb, &ctx);

    if (in_tokens_out)
        *in_tokens_out = ctx.in_tokens;
    if (out_tokens_out)
        *out_tokens_out = ctx.out_tokens;

    if (ctx.got_error)
        return -1;
    return rc;
}
