#include "test.h"
#include "nevercode.h"

static int test_sm_exact(void) {
    const char *L[] = {"hello", "world", "end"};
    const char *P[] = {"hello", "world"};
    int i = seek_match(L, 3, P, 2, 0, false);
    ASSERT_INT_EQ(i, 0);
    return 0;
}

static int test_sm_ws_trim(void) {
    const char *L[] = {"  hello  ", "world"};
    const char *P[] = {"hello", "world"};
    int i = seek_match(L, 2, P, 2, 0, false);
    ASSERT_INT_EQ(i, 0);
    return 0;
}

static int test_sm_eof_prefer(void) {
    const char *L[] = {"a", "b", "a", "b"};
    const char *P[] = {"a", "b"};
    int i = seek_match(L, 4, P, 2, 0, true);
    ASSERT_INT_EQ(i, 2);
    return 0;
}

static int test_sm_scored(void) {
    const char *L[] = {"x", "y"};
    const char *P[] = {"x", "y"};
    int q = 0;
    int i = seek_match_scored(L, 2, P, 2, 0, &q);
    ASSERT_INT_EQ(i, 0);
    ASSERT(q > 0);
    return 0;
}

static int test_sm_none(void) {
    const char *L[] = {"a", "b"};
    const char *P[] = {"z"};
    int i = seek_match(L, 2, P, 1, 0, false);
    ASSERT_INT_EQ(i, -1);
    return 0;
}

int test_seek_match_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("seek_match");
    RUN_TEST(test_sm_exact);
    RUN_TEST(test_sm_ws_trim);
    RUN_TEST(test_sm_eof_prefer);
    RUN_TEST(test_sm_scored);
    RUN_TEST(test_sm_none);
    TEST_RETURN_FAILED();
}
