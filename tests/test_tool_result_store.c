#include "test.h"
#include "nevercode.h"
#include <sys/stat.h>

int trs_init(const char *project_root);
char *trs_store(const char *call_id, const char *content, unsigned long len);
char *trs_load(const char *id);
char *trs_maybe_spill(const char *call_id, const char *content);
int trs_count(void);
unsigned long trs_total_bytes(void);
int trs_cleanup(int max_age_s);

static int test_init(void) {
    ASSERT_INT_EQ(trs_init("/tmp"), 0);
    ASSERT_INT_EQ(trs_count(), 0);
    return 0;
}

static int test_init_null(void) {
    ASSERT_INT_EQ(trs_init(NULL), -1);
    return 0;
}

static int test_store_load(void) {
    trs_init("/tmp");
    char *ref = trs_store("call-1", "test content here", 17);
    ASSERT_NOT_NULL(ref);
    ASSERT_STR_CONTAINS(ref, "call-1");
    ASSERT_INT_EQ(trs_count(), 1);
    ASSERT_TRUE(trs_total_bytes() > 0);

    char *loaded = trs_load("call-1");
    ASSERT_NOT_NULL(loaded);
    ASSERT_STR_EQ(loaded, "test content here");
    free(loaded);
    free(ref);
    return 0;
}

static int test_maybe_spill_small(void) {
    trs_init("/tmp");
    char *r = trs_maybe_spill("small-1", "short text");
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r, "short text");
    free(r);
    return 0;
}

static int test_store_null_args(void) {
    trs_init("/tmp");
    ASSERT_NULL(trs_store(NULL, "x", 1));
    ASSERT_NULL(trs_store("id", NULL, 0));
    ASSERT_NULL(trs_store("id", "x", 0));
    return 0;
}

static int test_load_nonexistent(void) {
    trs_init("/tmp");
    ASSERT_NULL(trs_load("nonexistent-id"));
    return 0;
}

int test_tool_result_store_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_result_store");
    RUN_TEST(test_init);
    RUN_TEST(test_init_null);
    RUN_TEST(test_store_load);
    RUN_TEST(test_maybe_spill_small);
    RUN_TEST(test_store_null_args);
    RUN_TEST(test_load_nonexistent);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("tool_result_store");
    RUN_TEST(test_init);
    RUN_TEST(test_init_null);
    RUN_TEST(test_store_load);
    RUN_TEST(test_maybe_spill_small);
    RUN_TEST(test_store_null_args);
    RUN_TEST(test_load_nonexistent);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
