#include "test.h"
#include "nevercode.h"
#include "tools.h"
#include <string.h>

static int test_tool_concurrency_safe_flag(void) {
    tool_def_t t = {0};
    t.name = "TestRead";
    t.concurrency_safe = true;
    ASSERT(t.concurrency_safe == true);

    tool_def_t t2 = {0};
    t2.name = "TestWrite";
    t2.concurrency_safe = false;
    ASSERT(t2.concurrency_safe == false);
    return 0;
}

static int test_read_tool_is_safe(void) {
    const tool_def_t *all = tools_get_all();
    int n = tools_count();
    bool found = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(all[i].name, "Read") == 0) {
            ASSERT(all[i].read_only == true);
            found = true;
            break;
        }
    }
    ASSERT(found);
    return 0;
}

static int test_grep_tool_is_safe(void) {
    const tool_def_t *t = tools_find("Grep");
    ASSERT_NOT_NULL(t);
    ASSERT(t->read_only == true);
    return 0;
}

static int test_write_tool_is_not_safe(void) {
    const tool_def_t *t = tools_find("Write");
    if (!t)
        return 0;
    ASSERT(t->read_only == false);
    return 0;
}

static int test_bash_readonly_detection(void) {
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "ls -la");
    ASSERT(bash_check_readonly(input) == true);
    cJSON_Delete(input);

    input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "rm -rf /tmp/test");
    ASSERT(bash_check_readonly(input) == false);
    cJSON_Delete(input);
    return 0;
}

static int test_api_retry_classify_rate_limit(void) {
    int cls = api_retry_classify("429 rate_limit exceeded");
    ASSERT(cls != 0);
    ASSERT(api_retry_should_retry(cls) == true);
    return 0;
}

static int test_api_retry_classify_auth(void) {
    int cls = api_retry_classify("401 unauthorized");
    ASSERT(api_retry_should_retry(cls) == false);
    return 0;
}

static int test_api_retry_classify_overloaded(void) {
    int cls = api_retry_classify("529 overloaded");
    ASSERT(api_retry_should_retry(cls) == true);
    return 0;
}

static int test_api_retry_delay_increases(void) {
    int cls = api_retry_classify("429 rate_limit");
    int d0 = api_retry_delay_ms(0, cls);
    int d1 = api_retry_delay_ms(1, cls);
    int d2 = api_retry_delay_ms(2, cls);
    ASSERT(d1 > d0);
    ASSERT(d2 > d1);
    return 0;
}

int test_batch_tools_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("batch_tools");
    tools_register_all();
    RUN_TEST(test_tool_concurrency_safe_flag);
    RUN_TEST(test_read_tool_is_safe);
    RUN_TEST(test_grep_tool_is_safe);
    RUN_TEST(test_write_tool_is_not_safe);
    RUN_TEST(test_bash_readonly_detection);
    RUN_TEST(test_api_retry_classify_rate_limit);
    RUN_TEST(test_api_retry_classify_auth);
    RUN_TEST(test_api_retry_classify_overloaded);
    RUN_TEST(test_api_retry_delay_increases);
    TEST_RETURN_FAILED();
}
