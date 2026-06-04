/*
 * remote_compact.c — Remote context compaction via secondary model
 *
 * Delegates conversation summarization to a cheaper/faster model,
 * freeing the primary model from compaction overhead.
 */

#include "nevercode.h"
#include "conversation.h"
#include "config.h"
#include "api.h"

static const char COMPACT_SYS[] =
    "You are a conversation compactor. Summarize the conversation into a concise "
    "summary preserving: decisions made, files modified, goals, findings, errors. "
    "Output ONLY the summary.";

static char *build_conv_text(const message_t *msgs, int n, int budget) {
    strbuf_t sb;
    strbuf_init(&sb);
    int tok = 0;

    for (int i = 0; i < n && tok < budget; i++) {
        const message_t *m = &msgs[i];
        const char *r = m->role == ROLE_USER        ? "user"
                        : m->role == ROLE_ASSISTANT ? "assistant"
                        : m->role == ROLE_TOOL      ? "tool"
                                                    : "system";

        for (int j = 0; j < m->num_blocks; j++) {
            const content_block_t *b = &m->blocks[j];
            if (!b->text || strcmp(b->text, COMPACT_SNIP_MARKER) == 0)
                continue;

            int t = estimate_tokens(b->text);
            if (tok + t > budget) {
                char *tr = middle_truncate_tokens(b->text, budget - tok);
                strbuf_appendf(&sb, "[%s] %s\n", r, tr ? tr : "");
                free(tr);
                tok = budget;
                break;
            }

            strbuf_appendf(&sb, "[%s] %s\n", r, b->text);
            tok += t;
        }
    }
    return strbuf_detach(&sb);
}

typedef struct {
    strbuf_t result;
    bool done;
    bool error;
} compact_cb_t;

static void compact_cb(const stream_event_t *ev, void *ud) {
    compact_cb_t *c = (compact_cb_t *)ud;
    if (ev->type == STREAM_EVENT_TEXT_DELTA && ev->text)
        strbuf_append_str(&c->result, ev->text);
    else if (ev->type == STREAM_EVENT_MESSAGE_STOP)
        c->done = true;
    else if (ev->type == STREAM_EVENT_ERROR) {
        c->error = true;
        c->done = true;
    }
}

char *remote_compact_summarize(const message_t *msgs, int n, const char *base_url,
                               const char *api_key, const char *model, provider_type_t provider) {
    if (!msgs || n <= 0 || !base_url || !api_key || !model)
        return NULL;

    char *ct = build_conv_text(msgs, n, REMOTE_COMPACT_INPUT_BUDGET);
    if (!ct)
        return NULL;

    int len = (int)strlen(ct) + 64;
    char *um = malloc(len);
    if (!um) {
        free(ct);
        return NULL;
    }
    snprintf(um, len, "Summarize this conversation:\n\n%s", ct);
    free(ct);

    message_t cm[1];
    memset(cm, 0, sizeof(cm));
    cm[0].role = ROLE_USER;
    content_block_t blk = {.type = CONTENT_TEXT, .text = um};
    cm[0].blocks = &blk;
    cm[0].num_blocks = 1;

    compact_cb_t cb;
    strbuf_init(&cb.result);
    cb.done = false;
    cb.error = false;

    config_t tc;
    memset(&tc, 0, sizeof(tc));
    tc.provider = provider;
    tc.model = nc_xstrdup(model);
    tc.max_tokens = REMOTE_COMPACT_OUTPUT_BUDGET;

    if (provider == PROVIDER_NATIVE) {
        tc.native_base_url = nc_xstrdup(base_url);
        tc.native_api_key = nc_xstrdup(api_key);
    } else {
        tc.compat_base_url = nc_xstrdup(base_url);
        tc.compat_api_key = nc_xstrdup(api_key);
    }

    int rc = api_stream_request(cm, 1, COMPACT_SYS, &tc, NULL, 0, compact_cb, &cb);

    free(um);
    free(tc.model);
    free(tc.native_base_url);
    free(tc.native_api_key);
    free(tc.compat_base_url);
    free(tc.compat_api_key);

    if (rc != 0 || cb.error || cb.result.len == 0) {
        strbuf_free(&cb.result);
        return NULL;
    }
    return strbuf_detach(&cb.result);
}

bool remote_compact_available(const void *cfg_ptr) {
    if (!cfg_ptr)
        return false;
    const config_t *cfg = (const config_t *)cfg_ptr;
    return config_base_url(cfg) && config_api_key(cfg) && config_model(cfg);
}
