#include "test.h"
#include "nevercode.h"
static int mock_ok(const cJSON *in, char **out, void *c) {
    (void)in;
    (void)c;
    *out = nc_xstrdup("ok");
    return 0;
}
static int mock_fail(const cJSON *in, char **out, void *c) {
    (void)in;
    (void)c;
    *out = nc_xstrdup("err");
    return 1;
}
static tool_def_t T_OK = {.name = "mock_ok", .execute = mock_ok, .concurrency_safe = true};
static tool_def_t T_FAIL
    __attribute__((unused)) = {.name = "mock_fail", .execute = mock_fail, .concurrency_safe = true};
static int t1(void) {
    pexec_init(4, 5000, NULL);
    cJSON *in = cJSON_CreateObject();
    ASSERT_INT_EQ(pexec_enqueue(&T_OK, in), 0);
    pexec_launch_all();
    pexec_wait_all();
    ASSERT_TRUE(pexec_all_done());
    ASSERT_INT_EQ(pexec_slot_rc(0), 0);
    cJSON_Delete(in);
    pexec_free();
    return 0;
}
static int t2(void) {
    pexec_init(4, 5000, NULL);
    cJSON *in = cJSON_CreateObject();
    for (int i = 0; i < 3; i++)
        pexec_enqueue(&T_OK, in);
    pexec_launch_all();
    pexec_wait_all();
    ASSERT_INT_EQ(pexec_done_count(), 3);
    cJSON_Delete(in);
    pexec_free();
    return 0;
}
static int t3(void) {
    pexec_init(2, 5000, NULL);
    ASSERT_NULL(pexec_slot_output(-1));
    pexec_free();
    return 0;
}
int test_parallel_exec_all(void) {
    TEST_SUITE("parallel_exec");
    TEST_COUNTERS;
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    TEST_RETURN_FAILED();
}
