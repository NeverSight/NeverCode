#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_init(void) {
    mem_phase_init();
    ASSERT_INT_EQ(mem_phase_count(), 0);
    ASSERT_FALSE(mem_phase_is_running());
    mem_phase_free();
    return 0;
}
static int test_add_raw(void) {
    mem_phase_init();
    int idx = mem_phase1_add_raw("Test memory content for extraction", "s1", 100);
    ASSERT(idx >= 0);
    ASSERT_INT_EQ(mem_phase_count(), 1);
    ASSERT_INT_EQ(mem_phase_raw_count(), 1);
    mem_phase_free();
    return 0;
}
static int test_min_tokens(void) {
    mem_phase_init();
    int idx = mem_phase1_add_raw("short", "s1", 5);
    ASSERT_INT_EQ(idx, -1);
    ASSERT_INT_EQ(mem_phase_count(), 0);
    mem_phase_free();
    return 0;
}
static int test_extract(void) {
    mem_phase_init();
    mem_phase1_add_raw("Extractable memory content here", "s1", 100);
    ASSERT_INT_EQ(mem_phase1_extract(0), 0);
    ASSERT_INT_EQ(mem_phase_extracted_count(), 1);
    ASSERT_INT_EQ(mem_phase_raw_count(), 0);
    mem_phase_free();
    return 0;
}
static int test_run_all(void) {
    mem_phase_init();
    mem_phase1_add_raw("Memory one for testing", "s1", 50);
    mem_phase1_add_raw("Memory two for testing", "s2", 60);
    int n = mem_phase1_run_all();
    ASSERT_INT_EQ(n, 2);
    ASSERT_INT_EQ(mem_phase_extracted_count(), 2);
    mem_phase_free();
    return 0;
}
static int test_consolidate(void) {
    mem_phase_init();
    mem_phase1_add_raw("Memory about database schema changes", "s1", 50);
    mem_phase1_add_raw("Memory about database schema changes", "s2", 50);
    mem_phase1_add_raw("Different topic: API refactoring work", "s3", 50);
    mem_phase1_run_all();
    int merged = mem_phase2_consolidate();
    ASSERT(merged >= 1);
    mem_phase_free();
    return 0;
}
static int test_format(void) {
    mem_phase_init();
    mem_phase1_add_raw("Test memory data", "s1", 100);
    char *s = mem_phase_format_status();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Memory phases");
    free(s);
    mem_phase_free();
    return 0;
}

int test_mem_phase_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("mem_phase");
    RUN_TEST(test_init);
    RUN_TEST(test_add_raw);
    RUN_TEST(test_min_tokens);
    RUN_TEST(test_extract);
    RUN_TEST(test_run_all);
    RUN_TEST(test_consolidate);
    RUN_TEST(test_format);
    TEST_RETURN_FAILED();
}
