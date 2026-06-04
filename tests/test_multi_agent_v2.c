#include "test.h"
#include "nevercode.h"
#include "tools.h"
#include "config.h"

static int test_agent_pool_init(void) {
    agent_pool_t pool;
    agent_pool_init(&pool);
    ASSERT_INT_EQ(pool.count, 0);
    ASSERT_INT_EQ(agent_pool_count(&pool), 0);
    agent_pool_free(&pool);
    return 0;
}

static int test_agent_pool_list_empty(void) {
    agent_pool_t pool;
    agent_pool_init(&pool);
    char *output = NULL;
    int rc = agent_pool_list(&pool, &output);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "No agents") != NULL);
    free(output);
    agent_pool_free(&pool);
    return 0;
}

static int test_agent_pool_wait_nonexistent(void) {
    agent_pool_t pool;
    agent_pool_init(&pool);
    char *output = NULL;
    int rc = agent_pool_wait(&pool, "no-such-id", &output);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "not found") != NULL);
    free(output);
    agent_pool_free(&pool);
    return 0;
}

static int test_agent_pool_close_nonexistent(void) {
    agent_pool_t pool;
    agent_pool_init(&pool);
    char *output = NULL;
    int rc = agent_pool_close(&pool, "no-such-id", &output);
    ASSERT_INT_EQ(rc, 1);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "not found") != NULL);
    free(output);
    agent_pool_free(&pool);
    return 0;
}

static int test_spawn_tool_registered(void) {
    tools_register_all();
    const tool_def_t *spawn = tools_find("SpawnAgent");
    ASSERT_NOT_NULL(spawn);
    ASSERT(spawn->concurrency_safe == false);
    ASSERT(spawn->destructive == false);

    const tool_def_t *list = tools_find("ListAgents");
    ASSERT_NOT_NULL(list);
    ASSERT(list->read_only == true);
    ASSERT(list->concurrency_safe == true);

    const tool_def_t *wait = tools_find("WaitAgent");
    ASSERT_NOT_NULL(wait);
    ASSERT(wait->read_only == true);

    const tool_def_t *close_t = tools_find("CloseAgent");
    ASSERT_NOT_NULL(close_t);
    ASSERT(close_t->read_only == false);
    return 0;
}

static int test_spawn_missing_message(void) {
    tools_register_all();
    const tool_def_t *spawn = tools_find("SpawnAgent");
    ASSERT_NOT_NULL(spawn);

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = spawn->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "missing") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_list_exec(void) {
    tools_register_all();
    const tool_def_t *list = tools_find("ListAgents");
    ASSERT_NOT_NULL(list);

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = list->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_wait_missing_id(void) {
    tools_register_all();
    const tool_def_t *wait = tools_find("WaitAgent");
    ASSERT_NOT_NULL(wait);

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = wait->execute(input, &output, NULL);
    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "missing") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_agent_pool_global(void) {
    agent_pool_t *p1 = agent_pool_global();
    agent_pool_t *p2 = agent_pool_global();
    ASSERT(p1 == p2);
    ASSERT_NOT_NULL(p1);
    return 0;
}

static int test_agent_pool_active_count(void) {
    agent_pool_t pool;
    agent_pool_init(&pool);
    ASSERT_INT_EQ(agent_pool_active_count(&pool), 0);
    agent_pool_free(&pool);
    return 0;
}

int test_multi_agent_v2_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("multi_agent_v2");

    RUN_TEST(test_agent_pool_init);
    RUN_TEST(test_agent_pool_list_empty);
    RUN_TEST(test_agent_pool_wait_nonexistent);
    RUN_TEST(test_agent_pool_close_nonexistent);
    RUN_TEST(test_spawn_tool_registered);
    RUN_TEST(test_spawn_missing_message);
    RUN_TEST(test_list_exec);
    RUN_TEST(test_wait_missing_id);
    RUN_TEST(test_agent_pool_global);
    RUN_TEST(test_agent_pool_active_count);

    TEST_RETURN_FAILED();
}
