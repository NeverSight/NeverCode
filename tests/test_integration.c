#include "test.h"
#include "nevercode.h"
#include "config.h"
#include "api.h"
#include "conversation.h"
#include "tools.h"
#include <curl/curl.h>
#include <signal.h>

/* g_interrupted is defined in repl_globals.c */

/* ---- shared state for stream callbacks ---- */

typedef struct {
    strbuf_t text;
    int got_stop;
    int stop_reason;
    int in_tokens;
    int out_tokens;
    int num_tc;
    char tc_name[256];
    char *error_msg;
} int_test_state_t;

static void state_init(int_test_state_t *s) {
    memset(s, 0, sizeof(*s));
    strbuf_init(&s->text);
    s->stop_reason = -1;
}

static void state_free(int_test_state_t *s) {
    strbuf_free(&s->text);
    free(s->error_msg);
    memset(s, 0, sizeof(*s));
}

static void simple_cb(const stream_event_t *ev, void *ud) {
    int_test_state_t *s = ud;
    switch (ev->type) {
    case STREAM_EVENT_TEXT_DELTA:
        if (ev->text)
            strbuf_append_str(&s->text, ev->text);
        break;
    case STREAM_EVENT_MESSAGE_STOP:
        s->got_stop = 1;
        s->stop_reason = ev->stop_reason;
        break;
    case STREAM_EVENT_USAGE:
        if (ev->input_tokens)
            s->in_tokens = ev->input_tokens;
        if (ev->output_tokens)
            s->out_tokens = ev->output_tokens;
        break;
    case STREAM_EVENT_TOOL_USE_BEGIN:
        s->num_tc++;
        if (ev->tool_name)
            nc_strlcpy(s->tc_name, ev->tool_name, sizeof(s->tc_name));
        break;
    case STREAM_EVENT_ERROR:
        if (ev->error_message)
            s->error_msg = nc_xstrdup(ev->error_message);
        break;
    default:
        break;
    }
}

static config_t make_test_config(void) {
    config_t cfg;
    config_init(&cfg);
    cfg.provider = PROVIDER_COMPAT;

    const char *key = getenv(ENV_COMPAT_KEY);
    const char *url = getenv(ENV_COMPAT_URL);
    const char *model = getenv(ENV_LIVE_MODEL);

    cfg.compat_api_key = nc_xstrdup(key ? key : "");
    cfg.model = nc_xstrdup(model ? model : nc_provider_default_model(PROVIDER_COMPAT));

    /* Ensure URL ends with /v1 (matches normalize_compat_url in config.c) */
    if (url && url[0]) {
        size_t ulen = strlen(url);
        while (ulen > 0 && url[ulen - 1] == '/')
            ulen--;
        if (ulen >= 3 && strncmp(url + ulen - 3, "/v1", 3) == 0) {
            cfg.compat_base_url = strndup(url, ulen);
        } else {
            char *buf = malloc(ulen + 4);
            memcpy(buf, url, ulen);
            memcpy(buf + ulen, "/v1", 4);
            cfg.compat_base_url = buf;
        }
    } else {
        cfg.compat_base_url = nc_xstrdup("");
    }
    cfg.max_tokens = 256;
    return cfg;
}

/* ---- test cases ---- */

static int test_api_simple_chat(void) {
    int_test_state_t st;
    state_init(&st);

    config_t cfg = make_test_config();

    message_t msgs[1];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "Say exactly 'hello nevercode test' and nothing else.");

    int rc = api_stream_request(msgs, 1, "You are a test assistant. Be brief.", &cfg, NULL, 0,
                                simple_cb, &st);

    fprintf(stderr, "  [integration] rc=%d, stop=%d, text_len=%zu\n", rc, st.got_stop, st.text.len);
    if (st.error_msg)
        fprintf(stderr, "  [integration] error: %s\n", st.error_msg);

    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(st.got_stop, 1);
    ASSERT_NOT_NULL(st.text.data);
    ASSERT(st.text.len > 0);

    message_free(&msgs[0]);
    config_free(&cfg);
    state_free(&st);
    return 0;
}

static int test_api_multi_turn(void) {
    int_test_state_t st;
    state_init(&st);

    config_t cfg = make_test_config();

    message_t msgs[3];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "Remember the number 42.");
    message_init(&msgs[1], ROLE_ASSISTANT);
    message_add_text(&msgs[1], "I'll remember: 42.");
    message_init(&msgs[2], ROLE_USER);
    message_add_text(&msgs[2],
                     "What number did I ask you to remember? Reply with just the number.");

    int rc = api_stream_request(msgs, 3, "Be concise.", &cfg, NULL, 0, simple_cb, &st);

    fprintf(stderr, "  [multi_turn] rc=%d, text=%s\n", rc, st.text.data ? st.text.data : "(null)");

    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(st.text.data);
    ASSERT(strstr(st.text.data, "42") != NULL);

    for (int i = 0; i < 3; i++)
        message_free(&msgs[i]);
    config_free(&cfg);
    state_free(&st);
    return 0;
}

static int test_api_tool_call(void) {
    int_test_state_t st;
    state_init(&st);

    config_t cfg = make_test_config();
    cfg.max_tokens = 512;

    tools_register_all();

    message_t msgs[1];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "Run the command: echo 'integration test passed'");

    int rc = api_stream_request(
        msgs, 1, "You are a coding assistant. When asked to run a command, use the Bash tool.",
        &cfg, tools_get_all(), tools_count(), simple_cb, &st);

    fprintf(stderr, "  [tool_call] rc=%d, stop=%d, tc=%d, name=%s\n", rc, st.got_stop, st.num_tc,
            st.tc_name);

    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(st.got_stop, 1);

    message_free(&msgs[0]);
    config_free(&cfg);
    state_free(&st);
    return 0;
}

static int test_api_token_usage(void) {
    int_test_state_t st;
    state_init(&st);

    config_t cfg = make_test_config();

    message_t msgs[1];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "Hi");

    int rc = api_stream_request(msgs, 1, "Be brief.", &cfg, NULL, 0, simple_cb, &st);

    fprintf(stderr, "  [usage] in=%d, out=%d\n", st.in_tokens, st.out_tokens);

    ASSERT_INT_EQ(rc, 0);
    /* token counts may or may not be returned depending on API */

    message_free(&msgs[0]);
    config_free(&cfg);
    state_free(&st);
    return 0;
}

int main(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    fprintf(stdout, "\033[1m=== nevercode integration tests ===\033[0m\n");

    const char *test_url = getenv(ENV_COMPAT_URL);
    const char *test_key = getenv(ENV_COMPAT_KEY);
    if (!test_url || !test_url[0] || !test_key || !test_key[0]) {
        fprintf(stdout, "\033[33mSkipped: set %s and %s to run\033[0m\n", ENV_COMPAT_URL,
                ENV_COMPAT_KEY);
        return 0;
    }
    fprintf(stdout, "\033[2mAPI: %s\033[0m\n", test_url);

    TEST_COUNTERS;
    TEST_SUITE("API streaming");
    RUN_TEST(test_api_simple_chat);
    RUN_TEST(test_api_multi_turn);
    RUN_TEST(test_api_tool_call);
    RUN_TEST(test_api_token_usage);

    fprintf(stdout, "\n\033[1m---\033[0m\n");
    if (_t_failed == 0)
        fprintf(stdout, "\033[32mAll integration tests passed!\033[0m\n\n");
    else
        fprintf(stdout, "\033[31m%d integration test(s) failed\033[0m\n\n", _t_failed);

    curl_global_cleanup();
    return _t_failed > 0 ? 1 : 0;
}
