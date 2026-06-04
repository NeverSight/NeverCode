/*
 * json_streamer.c — Incremental JSON parser for streaming tool arguments.
 *
 * Processes JSON object fragments character-by-character as they arrive
 * from a streaming API response. Emits per-field callbacks so the caller
 * can begin tool execution before the full argument block is complete.
 *
 * Pooled allocation avoids malloc churn across consecutive tool calls.
 */

#include "nevercode.h"
#include "defaults.h"

typedef enum {
    JS_IDLE,
    JS_OBJ,
    JS_KEY,
    JS_COLON,
    JS_VAL,
    JS_SVAL,
    JS_NVAL,
    JS_AVAL,
    JS_NEST,
    JS_DONE,
    JS_ERR
} js_st_t;

typedef struct {
    char key[JSON_STREAMER_KEY_MAX];
    char value[JSON_STREAMER_VALUE_PREVIEW];
    int value_len;
    bool complete;
} js_field_t;

struct json_streamer {
    strbuf_t buf;
    js_st_t state;
    int brace_depth;
    bool escaped;
    js_field_t fields[JSON_STREAMER_MAX_FIELDS];
    int num_fields;
    char cur_key[JSON_STREAMER_KEY_MAX];
    int key_pos;
    bool object_complete;
    int cursor;
    json_streamer_cb_t on_field;
    void *cb_ctx;
};

/* ── Pool ── */

static json_streamer_t *g_pool[JSON_STREAMER_POOL_SIZE];
static int g_pool_count;

/* ── Lifecycle ── */

json_streamer_t *json_streamer_create(json_streamer_cb_t cb, void *ctx) {
    json_streamer_t *js = calloc(1, sizeof(*js));
    if (!js)
        return NULL;
    strbuf_init(&js->buf);
    js->on_field = cb;
    js->cb_ctx = ctx;
    return js;
}

void json_streamer_free(json_streamer_t *js) {
    if (!js)
        return;
    strbuf_free(&js->buf);
    free(js);
}

void json_streamer_reset(json_streamer_t *js) {
    if (!js)
        return;
    strbuf_clear(&js->buf);
    js->state = JS_IDLE;
    js->brace_depth = 0;
    js->escaped = false;
    js->num_fields = 0;
    js->key_pos = 0;
    js->object_complete = false;
    js->cursor = 0;
    memset(js->cur_key, 0, sizeof(js->cur_key));
}

/* ── Field emission ── */

static void emit(json_streamer_t *js, const char *key, const char *val, int vl, bool complete) {
    if (js->num_fields < JSON_STREAMER_MAX_FIELDS) {
        js_field_t *f = &js->fields[js->num_fields];
        nc_strlcpy(f->key, key, sizeof(f->key));
        int c = vl < JSON_STREAMER_VALUE_PREVIEW - 1 ? vl : JSON_STREAMER_VALUE_PREVIEW - 1;
        if (c > 0 && val)
            memcpy(f->value, val, c);
        f->value[c] = '\0';
        f->value_len = vl;
        f->complete = complete;
        js->num_fields++;
    }
    if (js->on_field)
        js->on_field(key, val, vl, complete, js->cb_ctx);
}

/* ── Incremental parser ── */

int json_streamer_feed(json_streamer_t *js, const char *data, int len) {
    if (!js || !data || len <= 0)
        return -1;

    strbuf_append(&js->buf, data, len);
    const char *s = js->buf.data;
    int sl = (int)js->buf.len;
    int p = js->cursor;

    while (p < sl) {
        char c = s[p];

        switch (js->state) {
        case JS_IDLE:
            if (c == '{') {
                js->state = JS_OBJ;
                js->brace_depth = 1;
            }
            p++;
            break;

        case JS_OBJ:
            if (isspace((unsigned char)c)) {
                p++;
            } else if (c == '"') {
                js->state = JS_KEY;
                js->key_pos = 0;
                memset(js->cur_key, 0, sizeof(js->cur_key));
                p++;
            } else if (c == '}') {
                js->brace_depth--;
                if (js->brace_depth == 0) {
                    js->object_complete = true;
                    js->state = JS_DONE;
                }
                p++;
            } else if (c == ',') {
                p++;
            } else {
                js->state = JS_ERR;
                p++;
            }
            break;

        case JS_KEY:
            if (c == '\\') {
                js->escaped = true;
                p++;
            } else if (js->escaped) {
                if (js->key_pos < JSON_STREAMER_KEY_MAX - 1)
                    js->cur_key[js->key_pos++] = c;
                js->escaped = false;
                p++;
            } else if (c == '"') {
                js->cur_key[js->key_pos] = '\0';
                js->state = JS_COLON;
                p++;
            } else {
                if (js->key_pos < JSON_STREAMER_KEY_MAX - 1)
                    js->cur_key[js->key_pos++] = c;
                p++;
            }
            break;

        case JS_COLON:
            if (isspace((unsigned char)c)) {
                p++;
            } else if (c == ':') {
                js->state = JS_VAL;
                p++;
            } else {
                js->state = JS_ERR;
                p++;
            }
            break;

        case JS_VAL:
            if (isspace((unsigned char)c)) {
                p++;
            } else if (c == '"') {
                /* String value */
                p++;
                int vs = p, ve = vs;
                bool esc = false;
                while (ve < sl) {
                    if (s[ve] == '\\' && !esc) {
                        esc = true;
                        ve++;
                        continue;
                    }
                    if (esc) {
                        esc = false;
                        ve++;
                        continue;
                    }
                    if (s[ve] == '"') {
                        emit(js, js->cur_key, s + vs, ve - vs, true);
                        js->state = JS_OBJ;
                        p = ve + 1;
                        goto nxt;
                    }
                    ve++;
                }
                emit(js, js->cur_key, s + vs, sl - vs, false);
                js->cursor = p;
                return 0;
            } else if (c == '{') {
                /* Nested object */
                int ns = p, nd = 1;
                p++;
                while (p < sl && nd > 0) {
                    if (s[p] == '"') {
                        p++;
                        while (p < sl && s[p] != '"') {
                            if (s[p] == '\\')
                                p++;
                            p++;
                        }
                        if (p < sl)
                            p++;
                    } else {
                        if (s[p] == '{')
                            nd++;
                        else if (s[p] == '}')
                            nd--;
                        p++;
                    }
                }
                if (nd == 0) {
                    emit(js, js->cur_key, s + ns, p - ns, true);
                    js->state = JS_OBJ;
                } else {
                    emit(js, js->cur_key, s + ns, sl - ns, false);
                    js->cursor = p;
                    return 0;
                }
            } else if (c == '[') {
                /* Array value */
                int as = p, ad = 1;
                p++;
                while (p < sl && ad > 0) {
                    if (s[p] == '"') {
                        p++;
                        while (p < sl && s[p] != '"') {
                            if (s[p] == '\\')
                                p++;
                            p++;
                        }
                        if (p < sl)
                            p++;
                    } else {
                        if (s[p] == '[')
                            ad++;
                        else if (s[p] == ']')
                            ad--;
                        p++;
                    }
                }
                if (ad == 0) {
                    emit(js, js->cur_key, s + as, p - as, true);
                    js->state = JS_OBJ;
                } else {
                    emit(js, js->cur_key, s + as, sl - as, false);
                    js->cursor = p;
                    return 0;
                }
            } else {
                /* Literal (number, bool, null) */
                int ls = p;
                while (p < sl && !isspace((unsigned char)s[p]) && s[p] != ',' && s[p] != '}')
                    p++;
                if (p < sl) {
                    emit(js, js->cur_key, s + ls, p - ls, true);
                    js->state = JS_OBJ;
                } else {
                    js->cursor = p;
                    return 0;
                }
            }
            break;

        case JS_DONE:
        case JS_ERR:
            js->cursor = p;
            return js->state == JS_DONE ? 1 : -1;

        default:
            p++;
            break;
        }
    nxt:;
    }

    js->cursor = p;
    return js->object_complete ? 1 : 0;
}

/* ── Accessors ── */

bool json_streamer_complete(const json_streamer_t *js) {
    return js && js->object_complete;
}

int json_streamer_field_count(const json_streamer_t *js) {
    return js ? js->num_fields : 0;
}

cJSON *json_streamer_to_json(const json_streamer_t *js) {
    if (!js || !js->object_complete)
        return NULL;
    return cJSON_Parse(js->buf.data);
}

/* ── Pool management ── */

json_streamer_t *json_streamer_pool_acquire(json_streamer_cb_t cb, void *ctx) {
    if (g_pool_count > 0) {
        json_streamer_t *js = g_pool[--g_pool_count];
        json_streamer_reset(js);
        js->on_field = cb;
        js->cb_ctx = ctx;
        return js;
    }
    return json_streamer_create(cb, ctx);
}

void json_streamer_pool_release(json_streamer_t *js) {
    if (!js)
        return;
    if (g_pool_count < JSON_STREAMER_POOL_SIZE) {
        json_streamer_reset(js);
        g_pool[g_pool_count++] = js;
    } else {
        json_streamer_free(js);
    }
}
