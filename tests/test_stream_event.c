#include "test.h"
#include "nevercode.h"
#include "../third_party/cJSON/cJSON.h"

typedef struct stream_event_ctx_t stream_event_ctx_t;

void stream_event_init(stream_event_ctx_t *ctx);
void stream_event_free(stream_event_ctx_t *ctx);
int stream_event_process(stream_event_ctx_t *ctx, const cJSON *event);
const char *stream_event_get_content(const stream_event_ctx_t *ctx);
const char *stream_event_get_reasoning(const stream_event_ctx_t *ctx);
int stream_event_is_done(const stream_event_ctx_t *ctx);
int stream_event_tool_count(const stream_event_ctx_t *ctx);
char *stream_event_image_path(const char *home, const char *sid, const char *cid);

/* Allocate enough space for the opaque struct */
static char sev_buf[4096];

static int test_content_delta(void) {
    stream_event_ctx_t *ctx = (stream_event_ctx_t *)sev_buf;
    stream_event_init(ctx);

    cJSON *ev = cJSON_CreateObject();
    cJSON_AddStringToObject(ev, "type", "content.delta");
    cJSON_AddStringToObject(ev, "delta", "Hello ");
    stream_event_process(ctx, ev);
    cJSON_Delete(ev);

    ev = cJSON_CreateObject();
    cJSON_AddStringToObject(ev, "type", "content.delta");
    cJSON_AddStringToObject(ev, "delta", "world");
    stream_event_process(ctx, ev);
    cJSON_Delete(ev);

    ASSERT_STR_EQ(stream_event_get_content(ctx), "Hello world");
    ASSERT_FALSE(stream_event_is_done(ctx));

    stream_event_free(ctx);
    return 0;
}

static int test_done_event(void) {
    stream_event_ctx_t *ctx = (stream_event_ctx_t *)sev_buf;
    stream_event_init(ctx);

    cJSON *ev = cJSON_CreateObject();
    cJSON_AddStringToObject(ev, "type", "done");
    cJSON_AddStringToObject(ev, "finish_reason", "stop");
    stream_event_process(ctx, ev);
    cJSON_Delete(ev);

    ASSERT_TRUE(stream_event_is_done(ctx));
    stream_event_free(ctx);
    return 0;
}

static int test_tool_call_count(void) {
    stream_event_ctx_t *ctx = (stream_event_ctx_t *)sev_buf;
    stream_event_init(ctx);

    cJSON *ev = cJSON_CreateObject();
    cJSON_AddStringToObject(ev, "type", "tool_call.done");
    stream_event_process(ctx, ev);
    cJSON_Delete(ev);

    ev = cJSON_CreateObject();
    cJSON_AddStringToObject(ev, "type", "tool_call.done");
    stream_event_process(ctx, ev);
    cJSON_Delete(ev);

    ASSERT_INT_EQ(stream_event_tool_count(ctx), 2);
    stream_event_free(ctx);
    return 0;
}

static int test_image_path(void) {
    char *p = stream_event_image_path("/home/user", "session-1", "call/2");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "generated_images");
    ASSERT_STR_CONTAINS(p, "session-1");
    ASSERT_STR_CONTAINS(p, ".png");
    free(p);
    return 0;
}

static int test_null_event(void) {
    stream_event_ctx_t *ctx = (stream_event_ctx_t *)sev_buf;
    stream_event_init(ctx);
    ASSERT_INT_EQ(stream_event_process(ctx, NULL), -1);
    ASSERT_INT_EQ(stream_event_process(NULL, NULL), -1);
    stream_event_free(ctx);
    return 0;
}

/*
 * Regression: function_call_output must not be misclassified as
 * function_call.  strstr("function_call_output", "function_call") is
 * true, so the check order matters.
 *
 * Enum values: SEV_FUNCTION_CALL = 3, SEV_FUNCTION_OUTPUT = 4
 */
static int test_function_call_output_classification(void) {
    stream_event_ctx_t *ctx = (stream_event_ctx_t *)sev_buf;
    stream_event_init(ctx);

    cJSON *ev = cJSON_CreateObject();
    cJSON_AddStringToObject(ev, "type", "function_call_output");
    int kind = stream_event_process(ctx, ev);
    cJSON_Delete(ev);
    ASSERT_INT_EQ(kind, 4); /* SEV_FUNCTION_OUTPUT */

    ev = cJSON_CreateObject();
    cJSON_AddStringToObject(ev, "type", "function_call");
    kind = stream_event_process(ctx, ev);
    cJSON_Delete(ev);
    ASSERT_INT_EQ(kind, 3); /* SEV_FUNCTION_CALL */

    stream_event_free(ctx);
    return 0;
}

static int test_reasoning_accumulation(void) {
    stream_event_ctx_t *ctx = (stream_event_ctx_t *)sev_buf;
    stream_event_init(ctx);

    cJSON *ev = cJSON_CreateObject();
    cJSON_AddStringToObject(ev, "type", "reasoning.delta");
    cJSON_AddStringToObject(ev, "text", "Let me think");
    int kind = stream_event_process(ctx, ev);
    cJSON_Delete(ev);
    ASSERT_INT_EQ(kind, 5); /* SEV_REASONING */

    ev = cJSON_CreateObject();
    cJSON_AddStringToObject(ev, "type", "reasoning.delta");
    cJSON_AddStringToObject(ev, "text", " about this.");
    stream_event_process(ctx, ev);
    cJSON_Delete(ev);

    ASSERT_STR_EQ(stream_event_get_reasoning(ctx), "Let me think about this.");
    stream_event_free(ctx);
    return 0;
}

static int test_error_event(void) {
    stream_event_ctx_t *ctx = (stream_event_ctx_t *)sev_buf;
    stream_event_init(ctx);

    cJSON *ev = cJSON_CreateObject();
    cJSON_AddStringToObject(ev, "type", "error");
    int kind = stream_event_process(ctx, ev);
    cJSON_Delete(ev);
    ASSERT_INT_EQ(kind, 8); /* SEV_ERROR */

    stream_event_free(ctx);
    return 0;
}

int test_stream_event_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("stream_event");
    RUN_TEST(test_content_delta);
    RUN_TEST(test_done_event);
    RUN_TEST(test_tool_call_count);
    RUN_TEST(test_image_path);
    RUN_TEST(test_null_event);
    RUN_TEST(test_function_call_output_classification);
    RUN_TEST(test_reasoning_accumulation);
    RUN_TEST(test_error_event);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("stream_event");
    RUN_TEST(test_content_delta);
    RUN_TEST(test_done_event);
    RUN_TEST(test_tool_call_count);
    RUN_TEST(test_image_path);
    RUN_TEST(test_null_event);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
