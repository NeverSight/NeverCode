#include "test.h"
#include "nevercode.h"
static int t1(void) {
    env_ctx_init();
    ASSERT_INT_EQ(0, env_ctx_count());
    env_ctx_free();
    return 0;
}
static int t2(void) {
    env_ctx_init();
    env_ctx_set("foo", "bar");
    ASSERT_INT_EQ(1, env_ctx_count());
    ASSERT_STR_EQ("bar", env_ctx_get("foo"));
    env_ctx_free();
    return 0;
}
static int t3(void) {
    env_ctx_init();
    int n = env_ctx_probe_system();
    ASSERT_GT(n, 0);
    ASSERT_NOT_NULL(env_ctx_get("os_name"));
    char *s = env_ctx_build_prompt_section();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "environment_context");
    free(s);
    env_ctx_free();
    return 0;
}
static int t4(void) {
    env_ctx_init();
    char *s = env_ctx_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Environment");
    free(s);
    env_ctx_free();
    return 0;
}
int test_env_context_all(void) {
    TEST_SUITE("env_context");
    TEST_COUNTERS;
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    TEST_RETURN_FAILED();
}
