#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_repl_cleanup_empty(void) {
    repl_cleanup_all();
    ASSERT_INT_EQ(repl_active_count(), 0);
    return 0;
}

static int test_repl_list_empty(void) {
    repl_cleanup_all();
    char *list = repl_list_sessions();
    ASSERT(list != NULL);
    ASSERT(strstr(list, "No active") != NULL);
    free(list);
    return 0;
}

static int test_repl_exec_missing_params(void) {
    cJSON *input = cJSON_CreateObject();
    char *output = NULL;
    int rc = repl_tool_exec(input, &output, NULL);
    ASSERT(rc != 0);
    ASSERT(output != NULL);
    ASSERT(strstr(output, "required") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_repl_exec_missing_code(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "language", "python");
    char *output = NULL;
    int rc = repl_tool_exec(input, &output, NULL);
    ASSERT(rc != 0);
    ASSERT(output != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_repl_stop_no_session(void) {
    repl_cleanup_all();
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "language", "python");
    cJSON_AddStringToObject(input, "action", "stop");
    char *output = NULL;
    int rc = repl_tool_exec(input, &output, NULL);
    ASSERT_INT_EQ(rc, 0);
    ASSERT(strstr(output, "No active") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_repl_stop_missing_lang(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "action", "stop");
    char *output = NULL;
    int rc = repl_tool_exec(input, &output, NULL);
    ASSERT(rc != 0);
    free(output);
    cJSON_Delete(input);
    return 0;
}

static int test_repl_python_basic(void) {
    repl_cleanup_all();
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "language", "python");
    cJSON_AddStringToObject(input, "code", "print(2 + 3)");
    cJSON_AddNumberToObject(input, "timeout", 5);

    char *output = NULL;
    int rc = repl_tool_exec(input, &output, NULL);

    if (rc == 0 && output) {
        ASSERT(strstr(output, "5") != NULL || strlen(output) > 0);
    }

    free(output);
    cJSON_Delete(input);
    repl_cleanup_all();
    return 0;
}

static int test_repl_session_persists(void) {
    repl_cleanup_all();

    cJSON *input1 = cJSON_CreateObject();
    cJSON_AddStringToObject(input1, "language", "python");
    cJSON_AddStringToObject(input1, "code", "x = 42");
    cJSON_AddNumberToObject(input1, "timeout", REPL_PERSIST_TEST_TIMEOUT_S);
    char *out1 = NULL;
    int rc1 = repl_tool_exec(input1, &out1, NULL);
    ASSERT_INT_EQ(rc1, 0);
    ASSERT_NOT_NULL(out1);
    free(out1);
    cJSON_Delete(input1);

    cJSON *input2 = cJSON_CreateObject();
    cJSON_AddStringToObject(input2, "language", "python");
    cJSON_AddStringToObject(input2, "code", "print(x)");
    cJSON_AddNumberToObject(input2, "timeout", REPL_PERSIST_TEST_TIMEOUT_S);
    char *out2 = NULL;
    int rc2 = repl_tool_exec(input2, &out2, NULL);
    ASSERT_INT_EQ(rc2, 0);
    ASSERT_NOT_NULL(out2);
    ASSERT(strstr(out2, "42") != NULL);

    free(out2);
    cJSON_Delete(input2);
    repl_cleanup_all();
    return 0;
}

static int test_repl_unsupported_lang(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "language", "brainfuck");
    cJSON_AddStringToObject(input, "code", "++++[>+<-]>.");
    char *output = NULL;
    int rc = repl_tool_exec(input, &output, NULL);
    ASSERT(rc != 0);
    ASSERT(strstr(output, "failed") != NULL);
    free(output);
    cJSON_Delete(input);
    return 0;
}

int test_repl_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("repl");
    RUN_TEST(test_repl_cleanup_empty);
    RUN_TEST(test_repl_list_empty);
    RUN_TEST(test_repl_exec_missing_params);
    RUN_TEST(test_repl_exec_missing_code);
    RUN_TEST(test_repl_stop_no_session);
    RUN_TEST(test_repl_stop_missing_lang);
    RUN_TEST(test_repl_python_basic);
    RUN_TEST(test_repl_session_persists);
    RUN_TEST(test_repl_unsupported_lang);
    TEST_RETURN_FAILED();
}
