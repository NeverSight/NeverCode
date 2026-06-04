#include "test.h"
#include "nevercode.h"

void av2_init(void);
int av2_spawn(const char *id, const char *task_desc);
int av2_send_message(const char *from, const char *to, int kind, const char *payload);
int av2_set_state(const char *id, int state);
int av2_add_followup(const char *id, const char *followup_desc);
int av2_close(const char *id);
int av2_wait(const char *id, int timeout_ms);
int av2_count(void);
int av2_active_count(void);
char *av2_list_agents(void);
int av2_increment_turns(const char *id);
const char *av2_msg_kind_name(int k);

static int test_init(void) {
    av2_init();
    ASSERT_INT_EQ(av2_count(), 0);
    ASSERT_INT_EQ(av2_active_count(), 0);
    return 0;
}

static int test_spawn(void) {
    av2_init();
    ASSERT_INT_EQ(av2_spawn("agent-1", "do task A"), 0);
    ASSERT_INT_EQ(av2_count(), 1);
    ASSERT_INT_EQ(av2_active_count(), 1);

    ASSERT_INT_EQ(av2_spawn("agent-1", "dup"), -1); /* duplicate */
    return 0;
}

static int test_spawn_null(void) {
    av2_init();
    ASSERT_INT_EQ(av2_spawn(NULL, "x"), -1);
    return 0;
}

static int test_close(void) {
    av2_init();
    av2_spawn("a1", "task");
    ASSERT_INT_EQ(av2_close("a1"), 0);
    ASSERT_INT_EQ(av2_active_count(), 0);
    ASSERT_INT_EQ(av2_wait("a1", 0), 0); /* done */
    return 0;
}

static int test_followup(void) {
    av2_init();
    av2_spawn("f1", "initial task");
    ASSERT_INT_EQ(av2_add_followup("f1", "follow-up task"), 0);
    ASSERT_INT_EQ(av2_add_followup("nonexistent", "x"), -1);
    return 0;
}

static int test_increment_turns(void) {
    av2_init();
    av2_spawn("t1", "task");
    ASSERT_INT_EQ(av2_increment_turns("t1"), 1);
    ASSERT_INT_EQ(av2_increment_turns("t1"), 2);
    ASSERT_INT_EQ(av2_increment_turns("t1"), 3);
    ASSERT_INT_EQ(av2_increment_turns("nope"), -1);
    return 0;
}

static int test_list_agents(void) {
    av2_init();
    av2_spawn("x1", "explore");
    av2_spawn("x2", "review");

    char *list = av2_list_agents();
    ASSERT_NOT_NULL(list);
    ASSERT_STR_CONTAINS(list, "x1");
    ASSERT_STR_CONTAINS(list, "x2");
    ASSERT_STR_CONTAINS(list, "2 total");
    free(list);
    return 0;
}

static int test_send_receive(void) {
    av2_init();
    av2_spawn("sender", "send");
    av2_spawn("recv", "receive");

    ASSERT_INT_EQ(av2_send_message("sender", "recv", 0 /* TEXT */, "hello"), 0);

    /* Messages are in inbox; basic check */
    char *list = av2_list_agents();
    ASSERT_NOT_NULL(list);
    free(list);
    return 0;
}

static int test_msg_kind_names(void) {
    ASSERT_STR_EQ(av2_msg_kind_name(0), "text");
    ASSERT_STR_EQ(av2_msg_kind_name(1), "result");
    ASSERT_STR_EQ(av2_msg_kind_name(3), "followup");
    ASSERT_STR_EQ(av2_msg_kind_name(4), "shutdown");
    ASSERT_STR_EQ(av2_msg_kind_name(99), "unknown");
    return 0;
}

static int test_wait_running(void) {
    av2_init();
    av2_spawn("w1", "working");
    ASSERT_INT_EQ(av2_wait("w1", 0), 1); /* still running */
    av2_close("w1");
    ASSERT_INT_EQ(av2_wait("w1", 0), 0); /* done */
    return 0;
}

int test_agent_v2_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("agent_v2");
    RUN_TEST(test_init);
    RUN_TEST(test_spawn);
    RUN_TEST(test_spawn_null);
    RUN_TEST(test_close);
    RUN_TEST(test_followup);
    RUN_TEST(test_increment_turns);
    RUN_TEST(test_list_agents);
    RUN_TEST(test_send_receive);
    RUN_TEST(test_msg_kind_names);
    RUN_TEST(test_wait_running);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("agent_v2");
    RUN_TEST(test_init);
    RUN_TEST(test_spawn);
    RUN_TEST(test_spawn_null);
    RUN_TEST(test_close);
    RUN_TEST(test_followup);
    RUN_TEST(test_increment_turns);
    RUN_TEST(test_list_agents);
    RUN_TEST(test_send_receive);
    RUN_TEST(test_msg_kind_names);
    RUN_TEST(test_wait_running);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
