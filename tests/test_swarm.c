#include "test.h"
#include "nevercode.h"

static int test_init_phase(void) {
    swarm_init(4);
    ASSERT_INT_EQ((int)swarm_phase(), (int)SWARM_IDLE);
    swarm_free();
    return 0;
}

static int test_begin_and_work(void) {
    swarm_init(4);
    swarm_begin("solve challenge", 600);
    swarm_add_work("recon", 5);
    swarm_add_work("exploit", 10);
    int i = swarm_assign_next("agent-x");
    ASSERT_INT_EQ(i, 1);
    swarm_complete_work(i, "done", true);
    i = swarm_assign_next("agent-y");
    swarm_complete_work(i, "done", true);
    swarm_end();
    swarm_free();
    return 0;
}

static int test_phase_names(void) {
    ASSERT_STR_EQ(swarm_phase_name(SWARM_IDLE), "idle");
    ASSERT_STR_EQ(swarm_phase_name(SWARM_ACTIVE), "active");
    ASSERT_STR_EQ(swarm_phase_name(SWARM_COMPLETE), "complete");
    return 0;
}

static int test_no_work_assigns_neg(void) {
    swarm_init(2);
    swarm_begin("g", 60);
    int i = swarm_assign_next("a");
    ASSERT_INT_EQ(i, -1);
    swarm_end();
    swarm_free();
    return 0;
}

static int test_format(void) {
    swarm_init(3);
    swarm_begin("g", 30);
    swarm_add_work("w1", 1);
    char *f = swarm_format_status();
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "swarm");
    free(f);
    swarm_end();
    swarm_free();
    return 0;
}

int test_swarm_all(void) {
    TEST_SUITE("swarm");
    TEST_COUNTERS;
    RUN_TEST(test_init_phase);
    RUN_TEST(test_begin_and_work);
    RUN_TEST(test_phase_names);
    RUN_TEST(test_no_work_assigns_neg);
    RUN_TEST(test_format);
    TEST_RETURN_FAILED();
}
