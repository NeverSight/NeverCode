#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_init(void) {
    warmup_init(10000);
    ASSERT_INT_EQ(warmup_status(), WARMUP_IDLE);
    warmup_free();
    return 0;
}
static int test_start(void) {
    warmup_init(10000);
    warmup_start();
    ASSERT_INT_EQ(warmup_status(), WARMUP_RUNNING);
    warmup_free();
    return 0;
}
static int test_ready(void) {
    warmup_init(10000);
    warmup_start();
    warmup_mark_api_verified();
    warmup_mark_config_loaded();
    ASSERT_TRUE(warmup_is_ready());
    ASSERT_INT_EQ(warmup_status(), WARMUP_READY);
    warmup_free();
    return 0;
}
static int test_cancel(void) {
    warmup_init(10000);
    warmup_start();
    warmup_cancel();
    ASSERT_INT_EQ(warmup_status(), WARMUP_CANCELLED);
    warmup_free();
    return 0;
}
static int test_fail(void) {
    warmup_init(10000);
    warmup_start();
    warmup_fail();
    ASSERT_INT_EQ(warmup_status(), WARMUP_FAILED);
    warmup_free();
    return 0;
}
static int test_project_docs(void) {
    warmup_init(10000);
    ASSERT_FALSE(warmup_has_project_docs());
    warmup_mark_project_docs();
    ASSERT_TRUE(warmup_has_project_docs());
    warmup_free();
    return 0;
}
static int test_tools(void) {
    warmup_init(10000);
    warmup_set_tools(42);
    ASSERT_INT_EQ(warmup_tool_count(), 42);
    warmup_free();
    return 0;
}
static int test_status_names(void) {
    ASSERT_STR_EQ(warmup_status_name(WARMUP_IDLE), "idle");
    ASSERT_STR_EQ(warmup_status_name(WARMUP_READY), "ready");
    ASSERT_STR_EQ(warmup_status_name(WARMUP_TIMEOUT), "timeout");
    return 0;
}
static int test_format(void) {
    warmup_init(10000);
    warmup_start();
    warmup_mark_api_verified();
    char *s = warmup_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "API: OK");
    free(s);
    warmup_free();
    return 0;
}

int test_startup_warmup_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("startup_warmup");
    RUN_TEST(test_init);
    RUN_TEST(test_start);
    RUN_TEST(test_ready);
    RUN_TEST(test_cancel);
    RUN_TEST(test_fail);
    RUN_TEST(test_project_docs);
    RUN_TEST(test_tools);
    RUN_TEST(test_status_names);
    RUN_TEST(test_format);
    TEST_RETURN_FAILED();
}
