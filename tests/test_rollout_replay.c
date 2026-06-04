#include "test.h"
#include "nevercode.h"
#include <unistd.h>
#include <sys/stat.h>
static int t1(void) {
    ASSERT_INT_EQ(rollout_replay_event_count("nonexistent-xyz"), 0);
    return 0;
}
static int t2(void) {
    char *d = rollout_replay_describe("nonexistent-xyz");
    ASSERT_NOT_NULL(d);
    ASSERT_STR_CONTAINS(d, "no events");
    free(d);
    return 0;
}
static int t3(void) {
    message_t *m = NULL;
    int c = 0;
    ASSERT(rollout_replay_from_session("nonexistent-xyz", &m, &c, NULL) < 0);
    return 0;
}
static int t4(void) {
    ASSERT(rollout_replay_from_session(NULL, NULL, NULL, NULL) < 0);
    ASSERT_INT_EQ(rollout_replay_event_count(NULL), 0);
    return 0;
}
static int t5(void) {
    ASSERT_INT_EQ(rollout_replay_append_event("trr-001", "user", 1, "hello", NULL), 0);
    ASSERT_INT_EQ(rollout_replay_append_event("trr-001", "assistant", 1, "hi", NULL), 0);
    ASSERT(rollout_replay_event_count("trr-001") >= 2);
    return 0;
}
static int t6(void) {
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "tool", "bash");
    ASSERT_INT_EQ(rollout_replay_append_event("trr-meta", "tool_call", 2, "ls", m), 0);
    cJSON_Delete(m);
    return 0;
}
static int t7(void) {
    ASSERT(rollout_replay_append_event("x", NULL, 0, "d", NULL) < 0);
    return 0;
}
static int t8(void) {
    char *ids[32];
    int n = rollout_replay_list_sessions(ids, 32);
    ASSERT(n >= 0);
    for (int i = 0; i < n; i++)
        free(ids[i]);
    return 0;
}
static int t9(void) {
    rollout_replay_free_msgs(NULL, 0);
    return 0;
}
int test_rollout_replay_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("rollout_replay");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    TEST_RETURN_FAILED();
}
