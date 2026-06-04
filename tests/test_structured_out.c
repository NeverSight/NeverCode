#include "test.h"
#include "tools.h"
extern void tool_structured_out_register(void);
static int t1(void) {
    tool_structured_out_register();
    const tool_def_t *t = tools_find("StructuredOutput");
    ASSERT_NOT_NULL(t);
    ASSERT_TRUE(t->read_only);
    return 0;
}
static int t2(void) {
    tool_structured_out_register();
    const tool_def_t *t = tools_find("StructuredOutput");
    ASSERT_NOT_NULL(t);
    cJSON *in = cJSON_CreateObject();
    cJSON_AddStringToObject(in, "k", "v");
    char *out = NULL;
    ASSERT_INT_EQ(t->execute(in, &out, NULL), 0);
    ASSERT_NOT_NULL(out);
    ASSERT_TRUE(strstr(out, "structured_output") != NULL);
    free(out);
    cJSON_Delete(in);
    return 0;
}
int test_structured_out_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("structured_out");
    RUN_TEST(t1);
    RUN_TEST(t2);
    TEST_RETURN_FAILED();
}
