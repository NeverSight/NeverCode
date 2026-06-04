#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int t_init(void) {
    rollout_tracker_init("ts");
    ASSERT_INT_EQ(rollout_event_count(), 0);
    ASSERT_INT_EQ(rollout_current_turn(), 0);
    ASSERT(!rollout_is_dirty());
    rollout_tracker_free();
    return 0;
}

static int t_user_msg(void) {
    rollout_tracker_init("te");
    rollout_record_user_message("Hi");
    ASSERT_INT_EQ(rollout_event_count(), 1);
    ASSERT_INT_EQ(rollout_current_turn(), 1);
    ASSERT(rollout_is_dirty());
    rollout_record_user_message("Hello again");
    ASSERT_INT_EQ(rollout_event_count(), 2);
    ASSERT_INT_EQ(rollout_current_turn(), 2);
    rollout_tracker_free();
    return 0;
}

static int t_assistant_msg(void) {
    rollout_tracker_init("ta");
    rollout_record_user_message("Ping");
    rollout_record_assistant_message("Pong");
    ASSERT_INT_EQ(rollout_event_count(), 2);
    rollout_tracker_free();
    return 0;
}

static int t_tool_call(void) {
    rollout_tracker_init("tt");
    cJSON *inp = cJSON_CreateObject();
    cJSON_AddStringToObject(inp, "path", "/tmp/test");
    rollout_record_tool_call("read", "tc_001", inp);
    cJSON_Delete(inp);
    ASSERT_INT_EQ(rollout_event_count(), 1);
    rollout_record_tool_result("tc_001", "file contents", false);
    ASSERT_INT_EQ(rollout_event_count(), 2);
    rollout_tracker_free();
    return 0;
}

static int t_tool_result_error(void) {
    rollout_tracker_init("tre");
    rollout_record_tool_result("tc_002", "ENOENT", true);
    ASSERT_INT_EQ(rollout_event_count(), 1);
    rollout_tracker_free();
    return 0;
}

static int t_compaction(void) {
    rollout_tracker_init("tc");
    rollout_record_compaction(5000, 2000);
    ASSERT_INT_EQ(rollout_event_count(), 1);
    rollout_tracker_free();
    return 0;
}

static int t_handoff(void) {
    rollout_tracker_init("th");
    rollout_record_handoff("context exhaustion");
    ASSERT_INT_EQ(rollout_event_count(), 1);
    rollout_tracker_free();
    return 0;
}

static int t_checkpoint(void) {
    rollout_tracker_init("tck");
    rollout_record_user_message("start");
    rollout_record_checkpoint("turn-1");
    ASSERT_INT_EQ(rollout_event_count(), 2);
    rollout_tracker_free();
    return 0;
}

static int t_fork(void) {
    rollout_tracker_init("tf");
    rollout_record_fork("parent_session_abc", 5);
    ASSERT_INT_EQ(rollout_event_count(), 1);
    rollout_tracker_free();
    return 0;
}

static int t_settings(void) {
    rollout_tracker_init("tset");
    rollout_record_settings("model", "gpt-5.4");
    rollout_record_settings("effort", "high");
    ASSERT_INT_EQ(rollout_event_count(), 2);
    rollout_tracker_free();
    return 0;
}

static int t_full_session(void) {
    rollout_tracker_init("tfull");
    rollout_record_settings("model", "gpt-5.4");
    rollout_record_user_message("Write a test");
    rollout_record_assistant_message("I'll create a test file.");
    cJSON *inp = cJSON_CreateObject();
    cJSON_AddStringToObject(inp, "path", "test.c");
    rollout_record_tool_call("write", "tc_100", inp);
    cJSON_Delete(inp);
    rollout_record_tool_result("tc_100", "created", false);
    rollout_record_compaction(10000, 4000);
    rollout_record_user_message("Run it");
    rollout_record_assistant_message("Running tests...");
    rollout_record_checkpoint("turn-2");
    ASSERT_INT_EQ(rollout_event_count(), 9);
    ASSERT_INT_EQ(rollout_current_turn(), 2);
    ASSERT(rollout_is_dirty());
    rollout_tracker_free();
    return 0;
}

static int t_reinit(void) {
    rollout_tracker_init("r1");
    rollout_record_user_message("msg1");
    ASSERT_INT_EQ(rollout_event_count(), 1);
    rollout_tracker_free();
    rollout_tracker_init("r2");
    ASSERT_INT_EQ(rollout_event_count(), 0);
    ASSERT_INT_EQ(rollout_current_turn(), 0);
    rollout_tracker_free();
    return 0;
}

int test_rollout_tracker_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("rollout_tracker");
    RUN_TEST(t_init);
    RUN_TEST(t_user_msg);
    RUN_TEST(t_assistant_msg);
    RUN_TEST(t_tool_call);
    RUN_TEST(t_tool_result_error);
    RUN_TEST(t_compaction);
    RUN_TEST(t_handoff);
    RUN_TEST(t_checkpoint);
    RUN_TEST(t_fork);
    RUN_TEST(t_settings);
    RUN_TEST(t_full_session);
    RUN_TEST(t_reinit);
    TEST_RETURN_FAILED();
}
