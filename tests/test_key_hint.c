#include "test.h"
#include "nevercode.h"
#include "defaults.h"

static int test_idle_hints(void) {
    char *h = key_hint_render(KHINT_CTX_IDLE, 120);
    ASSERT_NOT_NULL(h);
    ASSERT_STR_CONTAINS(h, "send");
    ASSERT_STR_CONTAINS(h, "Enter");
    free(h);
    return 0;
}

static int test_streaming_hints(void) {
    char *h = key_hint_render(KHINT_CTX_STREAMING, 120);
    ASSERT_NOT_NULL(h);
    ASSERT_STR_CONTAINS(h, "stop");
    free(h);
    return 0;
}

static int test_plan_hints(void) {
    char *h = key_hint_render(KHINT_CTX_PLAN, 120);
    ASSERT_NOT_NULL(h);
    ASSERT_STR_CONTAINS(h, "plan");
    free(h);
    return 0;
}

static int test_vim_hints(void) {
    char *h = key_hint_render(KHINT_CTX_VIM, 120);
    ASSERT_NOT_NULL(h);
    ASSERT_STR_CONTAINS(h, "insert");
    free(h);
    return 0;
}

static int test_plain_render(void) {
    char *h = key_hint_render_plain(KHINT_CTX_IDLE);
    ASSERT_NOT_NULL(h);
    ASSERT(strstr(h, "\033[") == NULL);
    free(h);
    return 0;
}

static int test_count(void) {
    ASSERT_GT(key_hint_count(KHINT_CTX_IDLE), 0);
    ASSERT_GT(key_hint_count(KHINT_CTX_STREAMING), 0);
    ASSERT_GT(key_hint_count(KHINT_CTX_PLAN), 0);
    ASSERT_GT(key_hint_count(KHINT_CTX_VIM), 0);
    return 0;
}

static int test_oob_context(void) {
    char *h = key_hint_render(999, 80);
    ASSERT_NOT_NULL(h);
    free(h);
    return 0;
}

static int test_narrow_width(void) {
    char *h = key_hint_render(KHINT_CTX_IDLE, 10);
    ASSERT_NOT_NULL(h);
    free(h);
    return 0;
}

int test_key_hint_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("key_hint");
    RUN_TEST(test_idle_hints);
    RUN_TEST(test_streaming_hints);
    RUN_TEST(test_plan_hints);
    RUN_TEST(test_vim_hints);
    RUN_TEST(test_plain_render);
    RUN_TEST(test_count);
    RUN_TEST(test_oob_context);
    RUN_TEST(test_narrow_width);
    TEST_RETURN_FAILED();
}
