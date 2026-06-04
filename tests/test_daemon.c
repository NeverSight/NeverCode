/*
 * test_daemon.c — Tests for daemon mode and harness controller.
 */

#include "test.h"
#include "harness.h"
#include "conversation.h"
#include "defaults.h"

static int test_daemon_init(void) {
    daemon_ctx_t d;
    daemon_ctx_init(&d);
    ASSERT_INT_EQ(d.state, DAEMON_IDLE);
    ASSERT_INT_EQ(d.total_turns, 0);
    ASSERT_INT_EQ(d.total_failures, 0);
    ASSERT_INT_EQ(d.total_handoffs, 0);
    ASSERT_FALSE(daemon_is_running(&d));
    daemon_ctx_free(&d);
    return 0;
}

static int test_daemon_record_turn(void) {
    daemon_ctx_t d;
    daemon_ctx_init(&d);
    daemon_record_turn(&d);
    ASSERT_INT_EQ(d.total_turns, 1);
    ASSERT_INT_EQ(d.state, DAEMON_RUNNING);
    ASSERT_TRUE(daemon_is_running(&d));
    ASSERT_INT_EQ(d.fail.consecutive, 0);
    daemon_ctx_free(&d);
    return 0;
}

static int test_daemon_record_failure(void) {
    daemon_ctx_t d;
    daemon_ctx_init(&d);
    daemon_record_failure(&d, FAIL_TRANSIENT, "rate limited");
    ASSERT_INT_EQ(d.total_failures, 1);
    ASSERT_INT_EQ(d.fail.consecutive, 1);
    ASSERT_INT_EQ(d.fail.cls, FAIL_TRANSIENT);
    daemon_record_failure(&d, FAIL_PERSISTENT, "500 error");
    ASSERT_INT_EQ(d.fail.consecutive, 2);
    daemon_record_turn(&d);
    ASSERT_INT_EQ(d.fail.consecutive, 0);
    daemon_ctx_free(&d);
    return 0;
}

static int test_daemon_handoff(void) {
    daemon_ctx_t d;
    daemon_ctx_init(&d);
    daemon_record_handoff(&d);
    ASSERT_INT_EQ(d.total_handoffs, 1);
    daemon_ctx_free(&d);
    return 0;
}

static int test_daemon_uptime(void) {
    daemon_ctx_t d;
    daemon_ctx_init(&d);
    long up = daemon_uptime_s(&d);
    ASSERT_GE((int)up, 0);
    daemon_ctx_free(&d);
    return 0;
}

static int test_daemon_format(void) {
    daemon_ctx_t d;
    daemon_ctx_init(&d);
    daemon_record_turn(&d);
    daemon_record_turn(&d);
    char *fmt = daemon_format_status(&d);
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "running");
    ASSERT_STR_CONTAINS(fmt, "turns: 2");
    free(fmt);
    daemon_ctx_free(&d);
    return 0;
}

static int test_harness_init(void) {
    harness_ctx_t h;
    harness_init(&h);
    ASSERT_FALSE(h.needs_handoff);
    ASSERT_INT_EQ(h.context_pct, 0);
    harness_free(&h);
    return 0;
}

static int test_harness_post_turn(void) {
    harness_ctx_t h;
    harness_init(&h);
    harness_post_turn(&h, 50, 3, false);
    ASSERT_INT_EQ(h.context_pct, 50);
    ASSERT_FALSE(h.needs_handoff);
    harness_post_turn(&h, HARNESS_CONTEXT_EXHAUSTION_PCT, 1, false);
    ASSERT_TRUE(h.needs_handoff);
    harness_free(&h);
    return 0;
}

static int test_harness_handoff_trigger(void) {
    harness_ctx_t h;
    harness_init(&h);
    harness_post_turn(&h, HARNESS_CONTEXT_EXHAUSTION_PCT + 5, 2, false);
    ASSERT_TRUE(harness_needs_handoff(&h));
    harness_free(&h);
    return 0;
}

static int test_harness_handoff_extracts_state(void) {
    harness_ctx_t h;
    harness_init(&h);

    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "Refactor the auth module");

    message_t *am = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(am, "I will restructure the authentication layer.");

    cJSON *inp = cJSON_CreateObject();
    cJSON_AddStringToObject(inp, "file_path", "src/auth.c");
    message_add_tool_use(am, "call_1", "file_write", inp);
    cJSON_Delete(inp);

    harness_post_turn(&h, HARNESS_CONTEXT_EXHAUSTION_PCT + 5, 1, false);
    ASSERT_TRUE(harness_needs_handoff(&h));

    char *resume = harness_do_handoff(&h, &conv, "test_session");
    ASSERT_NOT_NULL(resume);
    ASSERT_STR_CONTAINS(resume, "Refactor the auth module");
    ASSERT_STR_CONTAINS(resume, "file_write");
    free(resume);

    ASSERT_FALSE(h.needs_handoff);
    ASSERT_NOT_NULL(h.handoff.goal_summary);
    ASSERT_NOT_NULL(h.handoff.files_modified);

    conversation_free(&conv);
    harness_free(&h);
    return 0;
}

static int test_harness_handoff_null_conv(void) {
    harness_ctx_t h;
    harness_init(&h);
    harness_post_turn(&h, HARNESS_CONTEXT_EXHAUSTION_PCT + 5, 1, false);

    char *resume = harness_do_handoff(&h, NULL, "test_null");
    ASSERT_NOT_NULL(resume);
    ASSERT_STR_CONTAINS(resume, "Context compacted");
    free(resume);

    harness_free(&h);
    return 0;
}

static int test_harness_handoff_empty_conv(void) {
    harness_ctx_t h;
    harness_init(&h);
    harness_post_turn(&h, HARNESS_CONTEXT_EXHAUSTION_PCT + 5, 1, false);

    conversation_t conv;
    conversation_init(&conv);

    char *resume = harness_do_handoff(&h, &conv, "test_empty");
    ASSERT_NOT_NULL(resume);
    free(resume);

    conversation_free(&conv);
    harness_free(&h);
    return 0;
}

static int test_harness_global(void) {
    harness_ctx_t *g = harness_global();
    ASSERT_NOT_NULL(g);
    harness_ctx_t *g2 = harness_global();
    ASSERT(g == g2);
    return 0;
}

int test_daemon_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("daemon");
    RUN_TEST(test_daemon_init);
    RUN_TEST(test_daemon_record_turn);
    RUN_TEST(test_daemon_record_failure);
    RUN_TEST(test_daemon_handoff);
    RUN_TEST(test_daemon_uptime);
    RUN_TEST(test_daemon_format);
    RUN_TEST(test_harness_init);
    RUN_TEST(test_harness_post_turn);
    RUN_TEST(test_harness_handoff_trigger);
    RUN_TEST(test_harness_handoff_extracts_state);
    RUN_TEST(test_harness_handoff_null_conv);
    RUN_TEST(test_harness_handoff_empty_conv);
    RUN_TEST(test_harness_global);
    TEST_RETURN_FAILED();
}
