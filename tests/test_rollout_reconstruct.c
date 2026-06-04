#include "test.h"
#include "nevercode.h"
static int t1(void) {
    message_t *m = NULL;
    int c = 0;
    ASSERT(rollout_replay_reconstruct(NULL, &m, &c, NULL, NULL) < 0);
    return 0;
}
static int t2(void) {
    message_t *m = NULL;
    int c = 0;
    ASSERT(rollout_replay_reconstruct("nonexist_xyz", &m, &c, NULL, NULL) < 0);
    return 0;
}
static int t3(void) {
    ASSERT(rollout_replay_drop_last_n_turns(NULL, 1) < 0);
    return 0;
}
static int t4(void) {
    ASSERT(rollout_replay_drop_last_n_turns("test_session", 0) < 0);
    return 0;
}
static int t5(void) {
    char *d = rollout_replay_describe("no_such_abc");
    ASSERT_NOT_NULL(d);
    ASSERT_STR_CONTAINS(d, "no events");
    free(d);
    return 0;
}
int test_rollout_reconstruct_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("rollout_reconstruct");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    TEST_RETURN_FAILED();
}
