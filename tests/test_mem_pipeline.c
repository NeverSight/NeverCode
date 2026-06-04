#include "test.h"
#include "nevercode.h"
static int t1(void) {
    mem_pipeline_init();
    ASSERT_TRUE(mem_pipeline_ready());
    ASSERT_INT_EQ(0, mem_pipeline_phase1_count());
    mem_pipeline_free();
    return 0;
}
static int t2(void) {
    mem_pipeline_init();
    message_t m;
    memset(&m, 0, sizeof(m));
    m.role = ROLE_USER;
    content_block_t b;
    memset(&b, 0, sizeof(b));
    b.type = CONTENT_TEXT;
    b.text = "Hello test";
    m.blocks = &b;
    m.num_blocks = 1;
    char *raw = NULL, *sum = NULL;
    int rc = mem_pipeline_phase1_extract("test_s", &m, 1, &raw, &sum);
    ASSERT_INT_EQ(0, rc);
    ASSERT_NOT_NULL(raw);
    ASSERT_NOT_NULL(sum);
    free(raw);
    free(sum);
    mem_pipeline_free();
    return 0;
}
static int t3(void) {
    mem_pipeline_init();
    int n = mem_pipeline_phase2_consolidate(50);
    ASSERT_GE(n, 0);
    mem_pipeline_free();
    return 0;
}
static int t4(void) {
    mem_pipeline_init();
    char *s = mem_pipeline_format_status();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Pipeline");
    free(s);
    mem_pipeline_free();
    return 0;
}
int test_mem_pipeline_all(void) {
    TEST_SUITE("mem_pipeline");
    TEST_COUNTERS;
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    TEST_RETURN_FAILED();
}
