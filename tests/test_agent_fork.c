#include "test.h"
#include "nevercode.h"

static int test_af_none(void) {
    agent_spawn_opts_t o = {.mode = FORK_NONE, .last_n = 0, .context_hint = NULL};
    ASSERT_INT_EQ(o.mode, FORK_NONE);
    ASSERT_INT_EQ(o.last_n, 0);
    ASSERT_NULL(o.context_hint);
    return 0;
}

static int test_af_full(void) {
    agent_spawn_opts_t o = {.mode = FORK_FULL, .last_n = 0, .context_hint = "ctx"};
    ASSERT_INT_EQ(o.mode, FORK_FULL);
    ASSERT_NOT_NULL(o.context_hint);
    ASSERT_STR_EQ(o.context_hint, "ctx");
    return 0;
}

static int test_af_lastn(void) {
    agent_spawn_opts_t o = {.mode = FORK_LAST_N, .last_n = 3, .context_hint = NULL};
    ASSERT_INT_EQ(o.mode, FORK_LAST_N);
    ASSERT_INT_EQ(o.last_n, 3);
    return 0;
}

static int test_af_default_constant(void) {
    ASSERT_TRUE(AGENT_FORK_LAST_N_DEFAULT > 0);
    ASSERT_INT_EQ(AGENT_FORK_LAST_N_DEFAULT, 5);
    return 0;
}

static int test_af_spawn_null_opts(void) {
    agent_pool_t p;
    agent_pool_init(&p);
    char *id = agent_pool_spawn_ex(&p, "task", NULL, NULL, NULL);
    free(id);
    agent_pool_free(&p);
    return 0;
}

static int test_af_enum_values(void) {
    ASSERT_INT_EQ(FORK_NONE, 0);
    ASSERT_INT_EQ(FORK_FULL, 1);
    ASSERT_INT_EQ(FORK_LAST_N, 2);
    return 0;
}

static int test_af_lastn_zero(void) {
    agent_spawn_opts_t o = {.mode = FORK_LAST_N, .last_n = 0, .context_hint = NULL};
    ASSERT_INT_EQ(o.last_n, 0);
    return 0;
}

int test_agent_fork_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("agent_fork");

    RUN_TEST(test_af_none);
    RUN_TEST(test_af_full);
    RUN_TEST(test_af_lastn);
    RUN_TEST(test_af_default_constant);
    RUN_TEST(test_af_spawn_null_opts);
    RUN_TEST(test_af_enum_values);
    RUN_TEST(test_af_lastn_zero);

    TEST_RETURN_FAILED();
}
