#include "test.h"
#include "nevercode.h"
#include "tools.h"

extern void tool_multi_agent_register(void);

static int test_send_message_registered(void) {
    tools_register_all();
    tool_multi_agent_register();
    const tool_def_t *t = tools_find("SendMessage");
    ASSERT_NOT_NULL(t);
    ASSERT(t->concurrency_safe == true);
    ASSERT(t->read_only == false);
    return 0;
}

static int test_send_message_no_agent(void) {
    agent_pool_t p;
    agent_pool_init(&p);
    int rc = agent_pool_send_message(&p, "nonexistent", "hello");
    ASSERT(rc == -1);
    agent_pool_free(&p);
    return 0;
}

static int test_send_message_null_args(void) {
    ASSERT(agent_pool_send_message(NULL, "id", "msg") == -1);
    agent_pool_t p;
    agent_pool_init(&p);
    ASSERT(agent_pool_send_message(&p, NULL, "msg") == -1);
    ASSERT(agent_pool_send_message(&p, "id", NULL) == -1);
    agent_pool_free(&p);
    return 0;
}

static int test_send_message_tool_exec(void) {
    tools_register_all();
    const tool_def_t *t = tools_find("SendMessage");
    ASSERT_NOT_NULL(t);

    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "agent_id", "fake-id");
    cJSON_AddStringToObject(input, "message", "test message");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT(rc == 1); /* agent not found */
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "not found") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_send_message_missing_params(void) {
    tools_register_all();
    const tool_def_t *t = tools_find("SendMessage");

    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    ASSERT(rc == 1);
    ASSERT(strstr(output, "missing") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

int test_send_message_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("send_message");
    RUN_TEST(test_send_message_registered);
    RUN_TEST(test_send_message_no_agent);
    RUN_TEST(test_send_message_null_args);
    RUN_TEST(test_send_message_tool_exec);
    RUN_TEST(test_send_message_missing_params);
    TEST_RETURN_FAILED();
}
