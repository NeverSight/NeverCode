/*
 * api_messages.c — Messages-format streaming API backend.
 *
 * Implements the Messages API protocol with SSE streaming,
 * prompt caching, and extended thinking support.
 */

#include "api_messages.h"
#include "harness.h"
#include "terminal.h"
#include <curl/curl.h>

static void sanitize_tool_id(char *dst, size_t sz, const char *src) {
    if (!src || !dst || sz == 0) {
        if (dst && sz > 0)
            dst[0] = '\0';
        return;
    }
    size_t j = 0;
    for (size_t i = 0; src[i] && j < sz - 1; i++) {
        char c = src[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-')
            dst[j++] = c;
        else
            dst[j++] = '_';
    }
    dst[j] = '\0';
}

static cJSON *block_to_json(const content_block_t *b) {
    cJSON *j = cJSON_CreateObject();
    switch (b->type) {
    case CONTENT_TEXT:
        cJSON_AddStringToObject(j, "type", "text");
        cJSON_AddStringToObject(j, "text", (b->text && b->text[0]) ? b->text : " ");
        break;
    case CONTENT_TOOL_USE: {
        cJSON_AddStringToObject(j, "type", "tool_use");
        char clean_id[NC_TAG_MAX];
        sanitize_tool_id(clean_id, sizeof(clean_id), b->tool_use_id);
        cJSON_AddStringToObject(j, "id", clean_id);
        cJSON_AddStringToObject(j, "name", b->tool_name);
        cJSON_AddItemToObject(
            j, "input", b->tool_input ? cJSON_Duplicate(b->tool_input, 1) : cJSON_CreateObject());
        break;
    }
    case CONTENT_TOOL_RESULT: {
        cJSON_AddStringToObject(j, "type", "tool_result");
        char clean_id2[NC_TAG_MAX];
        sanitize_tool_id(clean_id2, sizeof(clean_id2), b->tool_use_id);
        cJSON_AddStringToObject(j, "tool_use_id", clean_id2);
        cJSON_AddStringToObject(j, "content", b->text ? b->text : "");
        if (b->is_error)
            cJSON_AddTrueToObject(j, "is_error");
        break;
    }
    case CONTENT_IMAGE:
        cJSON_AddStringToObject(j, "type", "image");
        {
            cJSON *src = cJSON_CreateObject();
            cJSON_AddStringToObject(src, "type", "base64");
            cJSON_AddStringToObject(src, "media_type",
                                    b->image_media_type ? b->image_media_type : "image/png");
            cJSON_AddStringToObject(src, "data", b->image_data ? b->image_data : "");
            cJSON_AddItemToObject(j, "source", src);
        }
        break;
    }
    return j;
}

/*
 * Locate the last user-role message index for placing a cache breakpoint.
 * Caching everything up to this point allows the next turn to only
 * process the new assistant response.
 */
static int find_last_user_idx(const message_t *msgs, int n) {
    int last = -1;
    for (int i = 0; i < n; i++)
        if (msgs[i].role != ROLE_SYSTEM && msgs[i].role != ROLE_ASSISTANT)
            last = i;
    return last;
}

/* Place a second cache breakpoint at the midpoint to maximise prefix
 * cache reuse as conversations grow. */
static int find_mid_cache_idx(const message_t *msgs, int n, int last_user) {
    if (last_user < 4)
        return -1;
    int target = last_user / 2;
    for (int i = target; i >= 0; i--)
        if (msgs[i].role != ROLE_SYSTEM && msgs[i].role != ROLE_ASSISTANT)
            return i;
    return -1;
}

cJSON *messages_build_payload(const message_t *msgs, int n) {
    cJSON *arr = cJSON_CreateArray();
    const char *prev_role = NULL;
    cJSON *prev_content_arr = NULL;

    int last_user_idx = find_last_user_idx(msgs, n);
    int mid_cache_idx = find_mid_cache_idx(msgs, n, last_user_idx);

    for (int i = 0; i < n; i++) {
        const message_t *m = &msgs[i];
        if (m->role == ROLE_SYSTEM)
            continue;

        const char *role = (m->role == ROLE_ASSISTANT) ? "assistant" : "user";

        /*
         * The Messages API requires strict user/assistant alternation.
         * Merge consecutive same-role messages into one with combined blocks.
         */
        if (prev_role && strcmp(role, prev_role) == 0 && prev_content_arr) {
            for (int j = 0; j < m->num_blocks; j++) {
                cJSON *blk = block_to_json(&m->blocks[j]);
                if ((i == last_user_idx || i == mid_cache_idx) && j == m->num_blocks - 1) {
                    cJSON *cc = cJSON_CreateObject();
                    cJSON_AddStringToObject(cc, "type", "ephemeral");
                    cJSON_AddItemToObject(blk, "cache_control", cc);
                }
                cJSON_AddItemToArray(prev_content_arr, blk);
            }
            continue;
        }

        cJSON *jm = cJSON_CreateObject();
        cJSON_AddStringToObject(jm, "role", role);

        bool may_merge =
            (i + 1 < n && msgs[i + 1].role != ROLE_SYSTEM &&
             strcmp(role, (msgs[i + 1].role == ROLE_ASSISTANT) ? "assistant" : "user") == 0);
        bool needs_cache = (i == last_user_idx || i == mid_cache_idx) && !may_merge;

        if (!may_merge && m->num_blocks == 1 && m->blocks[0].type == CONTENT_TEXT && !needs_cache) {
            cJSON_AddStringToObject(jm, "content", m->blocks[0].text);
            prev_content_arr = NULL;
        } else {
            cJSON *content = cJSON_CreateArray();
            for (int j = 0; j < m->num_blocks; j++) {
                cJSON *blk = block_to_json(&m->blocks[j]);
                if (needs_cache && j == m->num_blocks - 1) {
                    cJSON *cc = cJSON_CreateObject();
                    cJSON_AddStringToObject(cc, "type", "ephemeral");
                    cJSON_AddItemToObject(blk, "cache_control", cc);
                }
                cJSON_AddItemToArray(content, blk);
            }
            cJSON_AddItemToObject(jm, "content", content);
            prev_content_arr = content;
        }
        cJSON_AddItemToArray(arr, jm);
        prev_role = role;
    }
    return arr;
}

static cJSON *build_tools_json(const tool_def_t *tools, int n) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", tools[i].name);
        cJSON_AddStringToObject(t, "description", tools[i].description);
        if (tools[i].input_schema)
            cJSON_AddItemToObject(t, "input_schema", cJSON_Duplicate(tools[i].input_schema, 1));
        if (i == n - 1) {
            cJSON *cc = cJSON_CreateObject();
            cJSON_AddStringToObject(cc, "type", "ephemeral");
            cJSON_AddItemToObject(t, "cache_control", cc);
        }
        cJSON_AddItemToArray(arr, t);
    }
    return arr;
}

/* ---- SSE streaming parser ---- */

typedef struct {
    strbuf_t buf;
    char event_type[NC_TAG_MAX];
    stream_callback_t cb;
    void *ud;
    long http_status;
    bool is_error;
    strbuf_t errbuf;
    int current_block_type; /* 0=unknown, 1=text, 2=tool_use, 3=thinking */
} msg_sse_t;

static void handle_msg_sse_event(msg_sse_t *ctx, const char *event, const char *data) {
    cJSON *root = cJSON_Parse(data);
    if (!root)
        return;

    stream_event_t ev = {0};

    if (strcmp(event, "content_block_start") == 0) {
        cJSON *cb_json = cJSON_GetObjectItem(root, "content_block");
        if (cb_json) {
            const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(cb_json, "type"));
            if (type && strcmp(type, "tool_use") == 0) {
                ctx->current_block_type = 2;
                ev.type = STREAM_EVENT_TOOL_USE_BEGIN;
                ev.tool_use_id = cJSON_GetStringValue(cJSON_GetObjectItem(cb_json, "id"));
                ev.tool_name = cJSON_GetStringValue(cJSON_GetObjectItem(cb_json, "name"));
                ctx->cb(&ev, ctx->ud);
            } else if (type && strcmp(type, "thinking") == 0) {
                ctx->current_block_type = 3;
            } else {
                ctx->current_block_type = 1;
            }
        }
    } else if (strcmp(event, "content_block_delta") == 0) {
        cJSON *delta = cJSON_GetObjectItem(root, "delta");
        if (delta) {
            const char *dtype = cJSON_GetStringValue(cJSON_GetObjectItem(delta, "type"));
            if (dtype && strcmp(dtype, "thinking_delta") == 0) {
                ev.type = STREAM_EVENT_THINKING_DELTA;
                ev.text = cJSON_GetStringValue(cJSON_GetObjectItem(delta, "thinking"));
                if (ev.text)
                    ctx->cb(&ev, ctx->ud);
            } else if (dtype && strcmp(dtype, "text_delta") == 0) {
                ev.type = STREAM_EVENT_TEXT_DELTA;
                ev.text = cJSON_GetStringValue(cJSON_GetObjectItem(delta, "text"));
                if (ev.text)
                    ctx->cb(&ev, ctx->ud);
            } else if (dtype && strcmp(dtype, "input_json_delta") == 0) {
                ev.type = STREAM_EVENT_TOOL_USE_DELTA;
                ev.json_fragment = cJSON_GetStringValue(cJSON_GetObjectItem(delta, "partial_json"));
                if (ev.json_fragment)
                    ctx->cb(&ev, ctx->ud);
            }
        }
    } else if (strcmp(event, "content_block_stop") == 0) {
        if (ctx->current_block_type == 2) {
            ev.type = STREAM_EVENT_TOOL_USE_END;
            ctx->cb(&ev, ctx->ud);
        }
        ctx->current_block_type = 0;
    } else if (strcmp(event, "message_start") == 0) {
        cJSON *message = cJSON_GetObjectItem(root, "message");
        if (message) {
            cJSON *usage = cJSON_GetObjectItem(message, "usage");
            if (usage) {
                ev.type = STREAM_EVENT_USAGE;
                cJSON *it = cJSON_GetObjectItem(usage, "input_tokens");
                cJSON *ot = cJSON_GetObjectItem(usage, "output_tokens");
                cJSON *cc = cJSON_GetObjectItem(usage, "cache_creation_input_tokens");
                cJSON *cr = cJSON_GetObjectItem(usage, "cache_read_input_tokens");
                if (it)
                    ev.input_tokens = (int)it->valuedouble;
                if (ot)
                    ev.output_tokens = (int)ot->valuedouble;
                if (cc) {
                    ev.cache_creation_tokens = (int)cc->valuedouble;
                    ev.input_tokens += ev.cache_creation_tokens;
                }
                if (cr) {
                    ev.cache_read_tokens = (int)cr->valuedouble;
                    ev.input_tokens += ev.cache_read_tokens;
                }
                ctx->cb(&ev, ctx->ud);
            }
        }
    } else if (strcmp(event, "message_delta") == 0) {
        cJSON *delta = cJSON_GetObjectItem(root, "delta");
        cJSON *usage = cJSON_GetObjectItem(root, "usage");
        if (usage) {
            stream_event_t uev = {0};
            uev.type = STREAM_EVENT_USAGE;
            cJSON *ot = cJSON_GetObjectItem(usage, "output_tokens");
            if (ot)
                uev.output_tokens = (int)ot->valuedouble;
            ctx->cb(&uev, ctx->ud);
        }
        if (delta) {
            const char *sr = cJSON_GetStringValue(cJSON_GetObjectItem(delta, "stop_reason"));
            ev.type = STREAM_EVENT_MESSAGE_STOP;
            ev.stop_reason = STOP_END_TURN;
            if (sr) {
                if (strcmp(sr, "tool_use") == 0)
                    ev.stop_reason = STOP_TOOL_USE;
                else if (strcmp(sr, "max_tokens") == 0)
                    ev.stop_reason = STOP_MAX_TOKENS;
            }
            ctx->cb(&ev, ctx->ud);
        }
    } else if (strcmp(event, "error") == 0) {
        cJSON *err = cJSON_GetObjectItem(root, "error");
        ev.type = STREAM_EVENT_ERROR;
        ev.error_message = cJSON_GetStringValue(cJSON_GetObjectItem(err ? err : root, "message"));
        ctx->cb(&ev, ctx->ud);
    }

    cJSON_Delete(root);
}

static void process_sse_lines(msg_sse_t *ctx) {
    if (!ctx->buf.data || ctx->buf.len == 0)
        return;
    while (1) {
        char *nl = strstr(ctx->buf.data, "\n\n");
        if (!nl)
            break;

        *nl = '\0';
        char *block = ctx->buf.data;
        char *data_line = NULL;

        char *line = block;
        while (*line) {
            char *eol = strchr(line, '\n');
            if (eol)
                *eol = '\0';

            if (nc_starts_with(line, "event: ")) {
                nc_strlcpy(ctx->event_type, line + 7, sizeof(ctx->event_type));
            } else if (nc_starts_with(line, "data: ")) {
                data_line = line + 6;
            }

            if (!eol)
                break;
            line = eol + 1;
        }

        if (data_line && ctx->event_type[0])
            handle_msg_sse_event(ctx, ctx->event_type, data_line);

        ctx->event_type[0] = '\0';

        size_t consumed = (nl + 2) - ctx->buf.data;
        size_t remaining = ctx->buf.len - consumed;
        memmove(ctx->buf.data, nl + 2, remaining);
        ctx->buf.len = remaining;
        ctx->buf.data[remaining] = '\0';
    }
}

static size_t header_cb(char *buf, size_t sz, size_t n, void *ud) {
    msg_sse_t *ctx = ud;
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

static size_t write_cb(char *ptr, size_t sz, size_t n, void *ud) {
    msg_sse_t *ctx = ud;
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

static int progress_cb(void *ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
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

int api_messages_stream(const message_t *messages, int num_messages, const char *system_prompt,
                        const config_t *cfg, const tool_def_t *tools, int num_tools,
                        stream_callback_t callback, void *userdata) {
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", config_model(cfg));
    cJSON_AddNumberToObject(body, "max_tokens", cfg->max_tokens);
    cJSON_AddBoolToObject(body, "stream", 1);
    if (system_prompt) {
        cJSON *sys_arr = cJSON_CreateArray();
        cJSON *block = cJSON_CreateObject();
        cJSON_AddStringToObject(block, "type", "text");
        cJSON_AddStringToObject(block, "text", system_prompt);
        cJSON *cc = cJSON_CreateObject();
        cJSON_AddStringToObject(cc, "type", "ephemeral");
        cJSON_AddItemToObject(block, "cache_control", cc);
        cJSON_AddItemToArray(sys_arr, block);
        cJSON_AddItemToObject(body, "system", sys_arr);
    }
    cJSON_AddItemToObject(body, "messages", messages_build_payload(messages, num_messages));
    if (tools && num_tools > 0)
        cJSON_AddItemToObject(body, "tools", build_tools_json(tools, num_tools));

    if (cfg->temperature >= 0)
        cJSON_AddNumberToObject(body, "temperature", cfg->temperature);
    if (cfg->top_p >= 0)
        cJSON_AddNumberToObject(body, "top_p", cfg->top_p);

    const char *model = config_model(cfg);
    if (model && model_supports_thinking(model)) {
        effort_apply_to_native_body(body, cfg->effort, model, cfg->max_tokens);
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

    char url[NC_URL_MAX];
    snprintf(url, sizeof(url), "%s/v1/messages", config_base_url(cfg));

    char auth_hdr[NC_MSG_MAX];
    snprintf(auth_hdr, sizeof(auth_hdr), "x-api-key: %s", config_api_key(cfg));

    msg_sse_t ctx = {0};
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
    hdrs = curl_slist_append(hdrs, API_HDR_NATIVE_VERSION);
    hdrs = curl_slist_append(hdrs, API_HDR_NATIVE_BETA);
    hdrs = curl_slist_append(hdrs, "Accept: text/event-stream");
    if (compressed)
        hdrs = curl_slist_append(hdrs, "Content-Encoding: gzip");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, send_body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)send_len);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)NC_CURL_CONNECT_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)NC_CURL_STREAM_TIMEOUT_SEC);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, (long)NC_CURL_LOW_SPEED_LIMIT);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, (long)NC_CURL_LOW_SPEED_TIME);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
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
        char errbuf[NC_ERRBUF_MAX];
        snprintf(errbuf, sizeof(errbuf), "HTTP %ld: %s", ctx.http_status,
                 api_msg ? api_msg : (ctx.errbuf.data ? ctx.errbuf.data : "unknown"));
        stream_event_t ev = {.type = STREAM_EVENT_ERROR, .error_message = errbuf};
        callback(&ev, userdata);
        if (err)
            cJSON_Delete(err);
        ret = -1;
    }

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    free(json_str);
    free(compressed_buf);
    strbuf_free(&ctx.buf);
    strbuf_free(&ctx.errbuf);
    return ret;
}
