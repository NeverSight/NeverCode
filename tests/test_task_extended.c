#include "test.h"
#include "tools.h"
#include "nevercode.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static int test_task_update_registered(void) {
    tools_register_all();
    const tool_def_t *t = tools_find("TaskUpdate");
    ASSERT_NOT_NULL(t);
    ASSERT(t->concurrency_safe == true);
    ASSERT_NOT_NULL(t->description);
    return 0;
}

static int test_task_output_registered(void) {
    const tool_def_t *t = tools_find("TaskOutput");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == true);
    ASSERT(t->concurrency_safe == true);
    return 0;
}

static int test_task_update_missing_params(void) {
    const tool_def_t *t = tools_find("TaskUpdate");
    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "missing") != NULL);
    free(output);
    return 0;
}

static int test_task_update_not_found(void) {
    const tool_def_t *t = tools_find("TaskUpdate");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "task_id", "fake-task-999");
    cJSON_AddStringToObject(input, "message", "hello");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "not found") != NULL);
    free(output);
    return 0;
}

static int test_task_output_missing_id(void) {
    const tool_def_t *t = tools_find("TaskOutput");
    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "missing") != NULL);
    free(output);
    return 0;
}

static int test_task_output_not_found(void) {
    const tool_def_t *t = tools_find("TaskOutput");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "task_id", "no-such-task");
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 1);
    ASSERT(strstr(output, "not found") != NULL);
    free(output);
    return 0;
}

static int test_task_output_running_partial(void) {
    agent_pool_t *pool = agent_pool_global();
    agent_pool_free(pool);
    agent_pool_init(pool);

    int pfd[2];
    ASSERT_INT_EQ(pipe(pfd), 0);
    ASSERT_NOT_NULL((pool->agents[0].agent_id = nc_xstrdup("agent-test-running")));
    pool->agents[0].status = AGENT_STATUS_RUNNING;
    pool->agents[0].pipe_out = pfd[0];
    pool->agents[0].pipe_in = -1;
    pool->agents[0].pid = -1; /* keep status as running without child process */
    pool->count = 1;

    const char *chunk = "hello from running task\nline2\n";
    ASSERT(write(pfd[1], chunk, strlen(chunk)) > 0);
    close(pfd[1]);

    const tool_def_t *t = tools_find("TaskOutput");
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "task_id", "agent-test-running");
    cJSON_AddNumberToObject(input, "tail_lines", 20);
    char *output = NULL;
    int rc = t->execute(input, &output, NULL);
    cJSON_Delete(input);

    ASSERT_INT_EQ(rc, 0);
    ASSERT_NOT_NULL(output);
    ASSERT(strstr(output, "\"status\"") != NULL);
    ASSERT(strstr(output, "running") != NULL || strstr(output, "completed") != NULL);
    ASSERT(strstr(output, "hello from running task") != NULL);
    free(output);

    agent_pool_free(pool);
    agent_pool_init(pool);
    return 0;
}

int test_task_extended_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("task_extended");

    RUN_TEST(test_task_update_registered);
    RUN_TEST(test_task_output_registered);
    RUN_TEST(test_task_update_missing_params);
    RUN_TEST(test_task_update_not_found);
    RUN_TEST(test_task_output_missing_id);
    RUN_TEST(test_task_output_not_found);
    RUN_TEST(test_task_output_running_partial);

    TEST_RETURN_FAILED();
}
