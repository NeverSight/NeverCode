#include "test.h"
#include "nevercode.h"
#include <string.h>

static int test_agent_pool_init_state(void) {
    agent_pool_t pool;
    agent_pool_init(&pool);
    ASSERT_INT_EQ(agent_pool_count(&pool), 0);
    ASSERT_INT_EQ(agent_pool_active_count(&pool), 0);
    agent_pool_free(&pool);
    return 0;
}

static int test_send_message_not_found(void) {
    agent_pool_t pool;
    agent_pool_init(&pool);
    int rc = agent_pool_send_message(&pool, "nonexistent", "hello");
    ASSERT_INT_EQ(rc, -1);
    agent_pool_free(&pool);
    return 0;
}

static int test_send_message_null_args(void) {
    int rc = agent_pool_send_message(NULL, "id", "msg");
    ASSERT_INT_EQ(rc, -1);

    agent_pool_t pool;
    agent_pool_init(&pool);
    rc = agent_pool_send_message(&pool, NULL, "msg");
    ASSERT_INT_EQ(rc, -1);
    rc = agent_pool_send_message(&pool, "id", NULL);
    ASSERT_INT_EQ(rc, -1);
    agent_pool_free(&pool);
    return 0;
}

static int test_live_agent_has_pipe_in(void) {
    live_agent_t a;
    memset(&a, 0, sizeof(a));
    a.pipe_in = -1;
    a.pipe_out = -1;
    ASSERT_INT_EQ(a.pipe_in, -1);
    ASSERT_INT_EQ(a.pipe_out, -1);
    return 0;
}

int test_agent_pipe_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("agent_pipe");
    RUN_TEST(test_agent_pool_init_state);
    RUN_TEST(test_send_message_not_found);
    RUN_TEST(test_send_message_null_args);
    RUN_TEST(test_live_agent_has_pipe_in);
    TEST_RETURN_FAILED();
}
