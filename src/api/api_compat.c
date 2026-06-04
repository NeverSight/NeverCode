/*
 * api_compat.c — Chat-completions compatible streaming API.
 *
 * Implements SSE streaming for /v1/chat/completions endpoints.
 */

#include "api_compat.h"
#include "harness.h"
#include "terminal.h"
#include <curl/curl.h>

/* ---- build chat-completions messages JSON ---- */

static bool s_use_developer_role = false;

void compat_set_developer_role(bool enabled) {
    s_use_developer_role = enabled;
}

cJSON *compat_build_messages(const message_t *msgs, int n, const char *sys) {
    cJSON *arr = cJSON_CreateArray();

    if (sys) {
        cJSON *sm = cJSON_CreateObject();
        cJSON_AddStringToObject(sm, "role", s_use_developer_role ? "developer" : "system");
        cJSON_AddStringToObject(sm, "content", sys);
        cJSON_AddItemToArray(arr, sm);
    }

    for (int i = 0; i < n; i++) {
        const message_t *m = &msgs[i];
        if (m->role == ROLE_SYSTEM)
            continue;

        if (m->role == ROLE_USER) {
            cJSON *jm = cJSON_CreateObject();
            cJSON_AddStringToObject(jm, "role", "user");

            bool has_image = false;
            for (int j = 0; j < m->num_blocks; j++) {
                if (m->blocks[j].type == CONTENT_IMAGE) {
                    has_image = true;
                    break;
                }
            }

            if (has_image) {
                cJSON *content_arr = cJSON_CreateArray();
                for (int j = 0; j < m->num_blocks; j++) {
                    const content_block_t *b = &m->blocks[j];
                    if (b->type == CONTENT_TEXT && b->text) {
                        cJSON *tb = cJSON_CreateObject();
                        cJSON_AddStringToObject(tb, "type", "text");
                        cJSON_AddStringToObject(tb, "text", b->text);
                        cJSON_AddItemToArray(content_arr, tb);
                    } else if (b->type == CONTENT_IMAGE && b->image_data) {
                        cJSON *ib = cJSON_CreateObject();
                        cJSON_AddStringToObject(ib, "type", "image_url");
                        cJSON *url_obj = cJSON_CreateObject();
                        strbuf_t url;
                        strbuf_init(&url);
                        strbuf_append_str(&url, "data:");
                        strbuf_append_str(&url,
                                          b->image_media_type ? b->image_media_type : "image/png");
                        strbuf_append_str(&url, ";base64,");
                        strbuf_append_str(&url, b->image_data);
                        cJSON_AddStringToObject(url_obj, "url", url.data ? url.data : "");
                        strbuf_free(&url);
                        cJSON_AddItemToObject(ib, "image_url", url_obj);
                        cJSON_AddItemToArray(content_arr, ib);
                    }
                }
                cJSON_AddItemToObject(jm, "content", content_arr);
            } else {
                strbuf_t sb;
                strbuf_init(&sb);
                for (int j = 0; j < m->num_blocks; j++) {
                    if (m->blocks[j].type == CONTENT_TEXT && m->blocks[j].text)
                        strbuf_append_str(&sb, m->blocks[j].text);
                }
                cJSON_AddStringToObject(jm, "content", sb.data ? sb.data : "");
                strbuf_free(&sb);
            }
            cJSON_AddItemToArray(arr, jm);
        } else if (m->role == ROLE_ASSISTANT) {
            cJSON *jm = cJSON_CreateObject();
            cJSON_AddStringToObject(jm, "role", "assistant");

            strbuf_t text_sb;
            strbuf_init(&text_sb);
            cJSON *tool_calls = NULL;

            for (int j = 0; j < m->num_blocks; j++) {
                const content_block_t *b = &m->blocks[j];
                if (b->type == CONTENT_TEXT && b->text)
                    strbuf_append_str(&text_sb, b->text);
                else if (b->type == CONTENT_TOOL_USE) {
                    if (!tool_calls)
                        tool_calls = cJSON_CreateArray();
                    cJSON *tc = cJSON_CreateObject();
                    cJSON_AddStringToObject(tc, "id", b->tool_use_id);
                    cJSON_AddStringToObject(tc, "type", "function");
                    cJSON *fn = cJSON_CreateObject();
                    cJSON_AddStringToObject(fn, "name", b->tool_name);
                    char *args =
                        b->tool_input ? cJSON_PrintUnformatted(b->tool_input) : nc_xstrdup("{}");
                    cJSON_AddStringToObject(fn, "arguments", args);
                    free(args);
                    cJSON_AddItemToObject(tc, "function", fn);
                    cJSON_AddItemToArray(tool_calls, tc);
                }
            }

            if (tool_calls && (!text_sb.data || text_sb.len == 0))
                cJSON_AddNullToObject(jm, "content");
            else
                cJSON_AddStringToObject(jm, "content", text_sb.data ? text_sb.data : "");
            strbuf_free(&text_sb);
            if (tool_calls)
                cJSON_AddItemToObject(jm, "tool_calls", tool_calls);
            cJSON_AddItemToArray(arr, jm);
        } else if (m->role == ROLE_TOOL) {
            for (int j = 0; j < m->num_blocks; j++) {
                const content_block_t *b = &m->blocks[j];
                if (b->type == CONTENT_TOOL_RESULT) {
                    cJSON *jm = cJSON_CreateObject();
                    cJSON_AddStringToObject(jm, "role", "tool");
                    cJSON_AddStringToObject(jm, "tool_call_id", b->tool_use_id);
                    cJSON_AddStringToObject(jm, "content", b->text ? b->text : "");
                    cJSON_AddItemToArray(arr, jm);
                }
            }
        }
    }
    return arr;
}

static cJSON *build_tools_json(const tool_def_t *tools, int n) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "type", "function");
        cJSON *fn = cJSON_CreateObject();
        cJSON_AddStringToObject(fn, "name", tools[i].name);
        cJSON_AddStringToObject(fn, "description", tools[i].description);
        if (tools[i].input_schema)
            cJSON_AddItemToObject(fn, "parameters", cJSON_Duplicate(tools[i].input_schema, 1));
        cJSON_AddItemToObject(t, "function", fn);
        cJSON_AddItemToArray(arr, t);
    }
    return arr;
}

/* ---- SSE event parser ---- */

typedef struct {
    char id[NC_TAG_MAX];
    char name[NC_LABEL_MAX];
    strbuf_t args;
    bool started;
} sse_tc_t;

typedef struct {
    strbuf_t buf;
    stream_callback_t cb;
    void *ud;
    long http_status;
    bool is_error;
    strbuf_t errbuf;
    sse_tc_t tcs[NEVERCODE_MAX_TOOL_CALLS];
    int num_tcs;
} sse_ctx_t;

/* Tool-call arguments: string or structured JSON, depending on gateway behavior. */
static char *compat_dup_function_arguments(const cJSON *fn) {
    if (!fn)
        return NULL;
    cJSON *a = cJSON_GetObjectItem(fn, "arguments");
    if (!a)
        return NULL;
    if (cJSON_IsString(a))
        return nc_xstrdup(a->valuestring);
    if (cJSON_IsObject(a) || cJSON_IsArray(a))
        return cJSON_PrintUnformatted(a);
    return NULL;
}

static void handle_sse_chunk(sse_ctx_t *ctx, const char *data) {
    if (!data || !*data)
        return;
    if (strcmp(data, "[DONE]") == 0)
        return;

    cJSON *root = cJSON_Parse(data);
    if (!root)
        return;

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    cJSON *c0 = cJSON_GetArrayItem(choices, 0);
    if (!c0) {
        cJSON_Delete(root);
        return;
    }

    cJSON *delta = cJSON_GetObjectItem(c0, "delta");
    if (!delta)
        delta = cJSON_GetObjectItem(c0, "message");
    const char *finish = cJSON_GetStringValue(cJSON_GetObjectItem(c0, "finish_reason"));

    if (delta) {
        /* Handle reasoning/thinking content (o1/o3/o4 models) */
        const char *reasoning =
            cJSON_GetStringValue(cJSON_GetObjectItem(delta, "reasoning_content"));
        if (!reasoning)
            reasoning = cJSON_GetStringValue(cJSON_GetObjectItem(delta, "reasoning"));
        if (reasoning) {
            stream_event_t ev = {.type = STREAM_EVENT_THINKING_DELTA, .text = reasoning};
            ctx->cb(&ev, ctx->ud);
        }

        const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(delta, "content"));
        if (content) {
            stream_event_t ev = {.type = STREAM_EVENT_TEXT_DELTA, .text = content};
            ctx->cb(&ev, ctx->ud);
        }

        cJSON *tcs = cJSON_GetObjectItem(delta, "tool_calls");
        if (tcs) {
            int tc_count = cJSON_GetArraySize(tcs);
            for (int i = 0; i < tc_count; i++) {
                cJSON *tc = cJSON_GetArrayItem(tcs, i);
                cJSON *idx_node = cJSON_GetObjectItem(tc, "index");
                int idx = (idx_node && cJSON_IsNumber(idx_node)) ? (int)idx_node->valuedouble : i;
                if (idx < 0 || idx >= NEVERCODE_MAX_TOOL_CALLS)
                    continue;

                const char *tcid = cJSON_GetStringValue(cJSON_GetObjectItem(tc, "id"));
                cJSON *fn = cJSON_GetObjectItem(tc, "function");
                const char *fn_name =
                    fn ? cJSON_GetStringValue(cJSON_GetObjectItem(fn, "name")) : NULL;
                char *args_owned = compat_dup_function_arguments(fn);
                const char *fn_args = args_owned;
                if (!fn_args && fn)
                    fn_args = cJSON_GetStringValue(cJSON_GetObjectItem(fn, "arguments"));

                if (idx >= ctx->num_tcs)
                    ctx->num_tcs = idx + 1;
                sse_tc_t *slot = &ctx->tcs[idx];

                if (tcid)
                    nc_strlcpy(slot->id, tcid, sizeof(slot->id));
                if (fn_name)
                    nc_strlcpy(slot->name, fn_name, sizeof(slot->name));

                /* Auto-generate id if the model omitted it */
                if (!slot->id[0] && slot->name[0]) {
                    snprintf(slot->id, sizeof(slot->id), "call_auto_%d", idx);
                }

                if (!slot->started && slot->id[0] && slot->name[0]) {
                    slot->started = true;
                    strbuf_init(&slot->args);
                    stream_event_t ev = {.type = STREAM_EVENT_TOOL_USE_BEGIN,
                                         .tool_use_id = slot->id,
                                         .tool_name = slot->name};
                    ctx->cb(&ev, ctx->ud);
                }

                if (fn_args && fn_args[0]) {
                    strbuf_append_str(&slot->args, fn_args);
                    stream_event_t ev = {.type = STREAM_EVENT_TOOL_USE_DELTA,
                                         .json_fragment = fn_args};
                    ctx->cb(&ev, ctx->ud);
                }
                free(args_owned);
            }
        }
    }

    cJSON *usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        stream_event_t uev = {0};
        uev.type = STREAM_EVENT_USAGE;
        cJSON *pt = cJSON_GetObjectItem(usage, "prompt_tokens");
        cJSON *ct = cJSON_GetObjectItem(usage, "completion_tokens");
        if (pt)
            uev.input_tokens = (int)pt->valuedouble;
        if (ct)
            uev.output_tokens = (int)ct->valuedouble;

        cJSON *ptd = cJSON_GetObjectItem(usage, "prompt_tokens_details");
        if (ptd) {
            cJSON *cached = cJSON_GetObjectItem(ptd, "cached_tokens");
            if (cached && cJSON_IsNumber(cached))
                uev.cache_read_tokens = (int)cached->valuedouble;
        }

        ctx->cb(&uev, ctx->ud);
    }

    if (finish) {
        bool has_started_tcs = false;
        for (int i = 0; i < ctx->num_tcs; i++)
            if (ctx->tcs[i].started) {
                has_started_tcs = true;
                break;
            }

        bool is_tool_finish = (strcmp(finish, FINISH_REASON_TOOL_CALLS) == 0 ||
                               strcmp(finish, FINISH_REASON_FUNCTION_CALL) == 0);

        /* Small models / gateways may set finish_reason="stop" despite
         * having emitted tool_calls in the delta stream.  Treat as tool
         * finish if we saw any started tool calls. */
        if (is_tool_finish || (has_started_tcs && strcmp(finish, FINISH_REASON_STOP) == 0)) {
            for (int i = 0; i < ctx->num_tcs; i++) {
                if (ctx->tcs[i].started) {
                    stream_event_t ev = {.type = STREAM_EVENT_TOOL_USE_END};
                    ctx->cb(&ev, ctx->ud);
                }
            }
            stream_event_t ev = {.type = STREAM_EVENT_MESSAGE_STOP, .stop_reason = STOP_TOOL_USE};
            ctx->cb(&ev, ctx->ud);
        } else {
            int reason = STOP_END_TURN;
            if (strcmp(finish, FINISH_REASON_LENGTH) == 0)
                reason = STOP_MAX_TOKENS;
            stream_event_t ev = {.type = STREAM_EVENT_MESSAGE_STOP, .stop_reason = reason};
            ctx->cb(&ev, ctx->ud);
        }
    }

    cJSON_Delete(root);
}

static void process_sse_lines(sse_ctx_t *ctx) {
    if (!ctx->buf.data || ctx->buf.len == 0)
        return;
    while (1) {
        char *nl = strstr(ctx->buf.data, "\n\n");
        if (!nl) {
            char *single = strchr(ctx->buf.data, '\n');
            if (single && single[1] != '\n') {
                *single = '\0';
                char *line = ctx->buf.data;
                if (nc_starts_with(line, "data: "))
                    handle_sse_chunk(ctx, line + 6);
                size_t consumed = (single + 1) - ctx->buf.data;
                size_t rem = ctx->buf.len - consumed;
                memmove(ctx->buf.data, single + 1, rem);
                ctx->buf.len = rem;
                ctx->buf.data[rem] = '\0';
                continue;
            }
            break;
        }

        *nl = '\0';
        char *block = ctx->buf.data;

        char *line = block;
        while (*line) {
            char *eol = strchr(line, '\n');
            if (eol)
                *eol = '\0';

            if (nc_starts_with(line, "data: "))
                handle_sse_chunk(ctx, line + 6);

            if (!eol)
                break;
            line = eol + 1;
        }

        size_t consumed = (nl + 2) - ctx->buf.data;
        size_t remaining = ctx->buf.len - consumed;
        memmove(ctx->buf.data, nl + 2, remaining);
        ctx->buf.len = remaining;
        ctx->buf.data[remaining] = '\0';
    }
}

static size_t sse_header_cb(char *buf, size_t sz, size_t n, void *ud) {
    sse_ctx_t *ctx = ud;
    size_t len = sz * n;
    if (nc_starts_with(buf, "HTTP/")) {
        char *sp = strchr(buf, ' ');
        if (sp) {
            ctx->http_status = nc_safe_atol(sp + 1, 0);
            ctx->is_error = (ctx->http_status != HTTP_OK);
        }
    }
    return len;
}

static size_t sse_write_cb(char *ptr, size_t sz, size_t n, void *ud) {
    sse_ctx_t *ctx = ud;
    size_t bytes = sz * n;
    if (g_interrupted)
        return 0;
    if (ctx->is_error) {
        strbuf_append(&ctx->errbuf, ptr, bytes);
        return bytes;
    }
    strbuf_append(&ctx->buf, ptr, bytes);
    process_sse_lines(ctx);
    return bytes;
}

static int sse_progress_cb(void *ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
                           curl_off_t ulnow) {
    (void)ud;
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    if (g_interrupted)
        return 1;
    return watchdog_stream_xfer_poll() ? 1 : 0;
}

static bool g_stream_opts_unsupported = false;
static bool g_parallel_tc_unsupported = false;

int api_compat_stream(const message_t *messages, int num_messages, const char *system_prompt,
                      const config_t *cfg, const tool_def_t *tools, int num_tools,
                      stream_callback_t callback, void *userdata) {
    const char *model_name = config_model(cfg);
    s_use_developer_role = model_uses_developer_role(model_name);

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", model_name);
    if (model_uses_max_completion_tokens(model_name))
        cJSON_AddNumberToObject(body, "max_completion_tokens", cfg->max_tokens);
    else
        cJSON_AddNumberToObject(body, "max_tokens", cfg->max_tokens);
    cJSON_AddBoolToObject(body, "stream", 1);
    if (!g_stream_opts_unsupported) {
        cJSON *stream_opts = cJSON_CreateObject();
        cJSON_AddTrueToObject(stream_opts, "include_usage");
        cJSON_AddItemToObject(body, "stream_options", stream_opts);
    }
    effort_apply_to_compat_body(body, cfg->effort, model_name);
    if (cfg->text_verbosity)
        cJSON_AddStringToObject(body, "text_verbosity", cfg->text_verbosity);
    if (cfg->reasoning_summary && cfg->effort != EFFORT_DEFAULT)
        cJSON_AddStringToObject(body, "reasoning_summary", cfg->reasoning_summary);
    if (cfg->temperature >= 0)
        cJSON_AddNumberToObject(body, "temperature", cfg->temperature);
    if (cfg->top_p >= 0)
        cJSON_AddNumberToObject(body, "top_p", cfg->top_p);
    if (cfg->frequency_penalty != 0.0)
        cJSON_AddNumberToObject(body, "frequency_penalty", cfg->frequency_penalty);
    if (cfg->presence_penalty != 0.0)
        cJSON_AddNumberToObject(body, "presence_penalty", cfg->presence_penalty);
    if (cfg->seed >= 0)
        cJSON_AddNumberToObject(body, "seed", cfg->seed);
    if (cfg->num_stop_sequences > 0) {
        cJSON *stop_arr = cJSON_CreateArray();
        for (int i = 0; i < cfg->num_stop_sequences; i++)
            cJSON_AddItemToArray(stop_arr, cJSON_CreateString(cfg->stop_sequences[i]));
        cJSON_AddItemToObject(body, "stop", stop_arr);
    }
    cJSON_AddItemToObject(body, "messages",
                          compat_build_messages(messages, num_messages, system_prompt));
    if (tools && num_tools > 0) {
        cJSON_AddItemToObject(body, "tools", build_tools_json(tools, num_tools));
        if (!g_parallel_tc_unsupported)
            cJSON_AddBoolToObject(body, "parallel_tool_calls", 1);
    }

    char *json_str = nc_cjson_detach(body);

    size_t json_len = strlen(json_str);
    char *send_body = json_str;
    size_t send_len = json_len;
    bool compressed = false;
    char *compressed_buf = NULL;
    size_t compressed_len = 0;
    if (request_compress_maybe(json_str, json_len, &compressed_buf, &compressed_len, &compressed) ==
            0 &&
        compressed) {
        send_body = compressed_buf;
        send_len = compressed_len;
    }

    /* base_url already ends with /v1 */
    char url[NC_URL_MAX];
    snprintf(url, sizeof(url), "%s/chat/completions", config_base_url(cfg));

    char auth_hdr[NC_MSG_MAX];
    snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", config_api_key(cfg));

    sse_ctx_t ctx = {0};
    strbuf_init(&ctx.buf);
    strbuf_init(&ctx.errbuf);
    ctx.cb = callback;
    ctx.ud = userdata;

    CURL *curl = curl_easy_init();
    if (!curl) {
        free(send_body == json_str ? json_str : send_body);
        free(compressed_buf);
        return -1;
    }
    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, auth_hdr);
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    hdrs = curl_slist_append(hdrs, "Accept: text/event-stream");
    if (compressed)
        hdrs = curl_slist_append(hdrs, "Content-Encoding: gzip");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, send_body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)send_len);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sse_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, sse_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)NC_CURL_CONNECT_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)NC_CURL_STREAM_TIMEOUT_SEC);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, (long)NC_CURL_LOW_SPEED_LIMIT);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, (long)NC_CURL_LOW_SPEED_TIME);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, sse_progress_cb);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const char *proxy = nc_get_proxy_env();
    if (proxy)
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy);

    if (cfg->verbose) {
        fprintf(stderr, "[API] POST %s\n", url);
        fprintf(stderr, "[API] Model: %s\n", config_model(cfg));
        if (proxy && proxy[0])
            fprintf(stderr, "[API] Proxy: %s\n", proxy);
        size_t jlen = strlen(json_str);
        if (jlen > API_DEBUG_BODY_TRUNC)
            fprintf(stderr, "[API] Body (%zu bytes): %.2000s...\n", jlen, json_str);
        else
            fprintf(stderr, "[API] Body: %s\n", json_str);
    }

    CURLcode res = curl_easy_perform(curl);
    int ret = 0;
    const int user_cancel = g_interrupted ? 1 : 0;

    if (!ctx.is_error && ctx.buf.data && ctx.buf.len > 0) {
        strbuf_append(&ctx.buf, "\n\n", 2);
        process_sse_lines(&ctx);
    }

    if (user_cancel) {
        for (int i = 0; i < ctx.num_tcs; i++)
            strbuf_free(&ctx.tcs[i].args);
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(curl);
        free(json_str);
        free(compressed_buf);
        strbuf_free(&ctx.buf);
        strbuf_free(&ctx.errbuf);
        return 0;
    }

    if (res == CURLE_ABORTED_BY_CALLBACK) {
        watchdog_verdict_t wv = watchdog_stream_xfer_take_verdict();
        if (wv != WD_VERDICT_OK) {
            char *recovery = watchdog_recovery_prompt(wv);
            stream_event_t ev = {.type = STREAM_EVENT_ERROR,
                                 .error_message = recovery ? recovery : "turn deadline exceeded"};
            callback(&ev, userdata);
            free(recovery);
            ret = -1;
        }
    }

    if (ret == 0 && res != CURLE_OK) {
        stream_event_t ev = {.type = STREAM_EVENT_ERROR, .error_message = curl_easy_strerror(res)};
        callback(&ev, userdata);
        ret = -1;
    } else if (ret == 0 && ctx.is_error) {
        cJSON *err = cJSON_Parse(ctx.errbuf.data);
        const char *api_msg = NULL;
        if (err) {
            cJSON *e = cJSON_GetObjectItem(err, "error");
            if (e)
                api_msg = cJSON_GetStringValue(cJSON_GetObjectItem(e, "message"));
        }
        /* Auto-disable stream_options if the gateway rejects it */
        if (!g_stream_opts_unsupported && ctx.http_status == HTTP_BAD_REQUEST && ctx.errbuf.data &&
            strstr(ctx.errbuf.data, AERR_BODY_STREAM_OPTS)) {
            g_stream_opts_unsupported = true;
            if (err)
                cJSON_Delete(err);
            for (int i = 0; i < ctx.num_tcs; i++)
                strbuf_free(&ctx.tcs[i].args);
            curl_slist_free_all(hdrs);
            curl_easy_cleanup(curl);
            free(json_str);
            free(compressed_buf);
            strbuf_free(&ctx.buf);
            strbuf_free(&ctx.errbuf);
            return api_compat_stream(messages, num_messages, system_prompt, cfg, tools, num_tools,
                                     callback, userdata);
        }
        /* Auto-disable parallel_tool_calls if the gateway rejects it */
        if (!g_parallel_tc_unsupported && ctx.http_status == HTTP_BAD_REQUEST && ctx.errbuf.data &&
            strstr(ctx.errbuf.data, AERR_BODY_PARALLEL_TC)) {
            g_parallel_tc_unsupported = true;
            if (cfg->verbose)
                fprintf(stderr, "[API] Disabling parallel_tool_calls\n");
            if (err)
                cJSON_Delete(err);
            for (int i = 0; i < ctx.num_tcs; i++)
                strbuf_free(&ctx.tcs[i].args);
            curl_slist_free_all(hdrs);
            curl_easy_cleanup(curl);
            free(json_str);
            free(compressed_buf);
            strbuf_free(&ctx.buf);
            strbuf_free(&ctx.errbuf);
            return api_compat_stream(messages, num_messages, system_prompt, cfg, tools, num_tools,
                                     callback, userdata);
        }
        /* Auto-disable request compression if the gateway cannot decode it */
        if (compressed &&
            (ctx.http_status == HTTP_BAD_GATEWAY || ctx.http_status == HTTP_INTERNAL ||
             ctx.http_status == HTTP_UNAVAILABLE)) {
            request_compress_set_enabled(false);
            if (cfg->verbose)
                fprintf(stderr, "[API] Disabling request compression (HTTP %ld)\n",
                        ctx.http_status);
            if (err)
                cJSON_Delete(err);
            for (int i = 0; i < ctx.num_tcs; i++)
                strbuf_free(&ctx.tcs[i].args);
            curl_slist_free_all(hdrs);
            curl_easy_cleanup(curl);
            free(json_str);
            free(compressed_buf);
            strbuf_free(&ctx.buf);
            strbuf_free(&ctx.errbuf);
            return api_compat_stream(messages, num_messages, system_prompt, cfg, tools, num_tools,
                                     callback, userdata);
        }
        if (cfg->verbose && ctx.errbuf.data && ctx.errbuf.len > 0)
            fprintf(stderr, "[API] Error response (HTTP %ld): %.500s\n", ctx.http_status,
                    ctx.errbuf.data);
        char errbuf[NC_ERRBUF_MAX];
        snprintf(errbuf, sizeof(errbuf), "HTTP %ld: %s", ctx.http_status,
                 api_msg ? api_msg : (ctx.errbuf.data ? ctx.errbuf.data : "unknown"));
        stream_event_t ev = {.type = STREAM_EVENT_ERROR, .error_message = errbuf};
        callback(&ev, userdata);
        if (err)
            cJSON_Delete(err);
        ret = -1;
    }

    for (int i = 0; i < ctx.num_tcs; i++)
        strbuf_free(&ctx.tcs[i].args);

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    free(json_str);
    free(compressed_buf);
    strbuf_free(&ctx.buf);
    strbuf_free(&ctx.errbuf);
    return ret;
}
