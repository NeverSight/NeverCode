#include "test.h"
#include "nevercode.h"
static int t1(void) {
    orch_init();
    ASSERT_INT_EQ(0, orch_total_calls());
    orch_free();
    return 0;
}
static int t2(void) {
    ASSERT_STR_EQ("ok", orch_result_name(ORCH_OK));
    ASSERT_STR_EQ("denied", orch_result_name(ORCH_DENIED));
    ASSERT_STR_EQ("retry_exhausted", orch_result_name(ORCH_RETRY_EXHAUSTED));
    return 0;
}
static int t3(void) {
    orch_init();
    char *s = orch_format_stats();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Orchestrator");
    ASSERT_STR_CONTAINS(s, "empty=");
    free(s);
    orch_free();
    return 0;
}
static int ok_tool(const cJSON *in, char **out, void *ctx) {
    (void)in;
    (void)ctx;
    *out = nc_xstrdup("result");
    return 0;
}
static int empty_tool(const cJSON *in, char **out, void *ctx) {
    (void)in;
    (void)ctx;
    *out = NULL;
    return 0;
}
static int t_empty_detection(void) {
    orch_init();
    tool_def_t td = {.name = "test_empty",
                     .description = "t",
                     .execute = empty_tool,
                     .read_only = false,
                     .destructive = false};
    cJSON *inp = cJSON_CreateObject();
    char *out = NULL;
    orch_execute(&td, inp, &out, NULL);
    ASSERT_INT_EQ(1, orch_total_empty_results());
    free(out);
    cJSON_Delete(inp);
    orch_free();
    return 0;
}
static int t_ok_no_empty(void) {
    orch_init();
    tool_def_t td = {.name = "test_ok",
                     .description = "t",
                     .execute = ok_tool,
                     .read_only = false,
                     .destructive = false};
    cJSON *inp = cJSON_CreateObject();
    char *out = NULL;
    orch_execute(&td, inp, &out, NULL);
    ASSERT_INT_EQ(0, orch_total_empty_results());
    ASSERT_INT_EQ(1, orch_total_successes());
    free(out);
    cJSON_Delete(inp);
    orch_free();
    return 0;
}
int test_tool_orchestrate_all(void) {
    TEST_SUITE("tool_orchestrate");
    TEST_COUNTERS;
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t_empty_detection);
    RUN_TEST(t_ok_no_empty);
    TEST_RETURN_FAILED();
}
