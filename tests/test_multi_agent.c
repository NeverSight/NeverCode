#include "test.h"
#include "nevercode.h"

static int test_pool_init(void) {
    agent_pool_t *p = agent_pool_global();
    ASSERT_NOT_NULL(p);
    agent_pool_init(p);
    ASSERT_INT_EQ(agent_pool_count(p), 0);
    agent_pool_free(p);
    return 0;
}

static int test_pool_count_zero(void) {
    agent_pool_t *p = agent_pool_global();
    agent_pool_init(p);
    ASSERT_INT_EQ(agent_pool_count(p), 0);
    ASSERT_INT_EQ(agent_pool_active_count(p), 0);
    agent_pool_free(p);
    return 0;
}

static int test_pool_list_empty(void) {
    agent_pool_t *p = agent_pool_global();
    agent_pool_init(p);
    char *output = NULL;
    int rc = agent_pool_list(p, &output);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    free(output);
    agent_pool_free(p);
    return 0;
}

static int test_pool_free_idempotent(void) {
    agent_pool_t *p = agent_pool_global();
    agent_pool_init(p);
    agent_pool_free(p);
    agent_pool_free(p);
    return 0;
}

static int test_pool_wait_invalid(void) {
    agent_pool_t *p = agent_pool_global();
    agent_pool_init(p);
    char *output = NULL;
    int rc = agent_pool_wait(p, "nonexistent-id", &output);
    ASSERT_TRUE(rc != 0);
    agent_pool_free(p);
    return 0;
}

int test_multi_agent_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("multi_agent");
    RUN_TEST(test_pool_init);
    RUN_TEST(test_pool_count_zero);
    RUN_TEST(test_pool_list_empty);
    RUN_TEST(test_pool_free_idempotent);
    RUN_TEST(test_pool_wait_invalid);
    TEST_RETURN_FAILED();
}
