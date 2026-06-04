/*
 * test_side_query.c — Tests for /btw side query trigger and extraction
 */

#include "test.h"
#include "nevercode.h"

static int test_trigger_exact(void) {
    ASSERT_TRUE(side_query_is_trigger("/btw hello"));
    ASSERT_TRUE(side_query_is_trigger("/btw"));
    ASSERT_FALSE(side_query_is_trigger("/btwx"));
    ASSERT_FALSE(side_query_is_trigger("btw hello"));
    ASSERT_FALSE(side_query_is_trigger(NULL));
    ASSERT_FALSE(side_query_is_trigger(""));
    ASSERT_FALSE(side_query_is_trigger("/"));
    return 0;
}

static int test_extract_question(void) {
    const char *q;

    q = side_query_extract("/btw what is quicksort?");
    ASSERT_NOT_NULL(q);
    ASSERT_STR_EQ(q, "what is quicksort?");

    q = side_query_extract("/btw   padded  ");
    ASSERT_NOT_NULL(q);
    ASSERT_STR_EQ(q, "padded  ");

    q = side_query_extract("/btw");
    ASSERT_NULL(q);

    q = side_query_extract("/btw   ");
    ASSERT_NULL(q);

    q = side_query_extract("hello world");
    ASSERT_NULL(q);

    q = side_query_extract(NULL);
    ASSERT_NULL(q);
    return 0;
}

static int test_trigger_word_boundary(void) {
    ASSERT_TRUE(side_query_is_trigger("/btw "));
    ASSERT_FALSE(side_query_is_trigger("/btw_wrong"));
    ASSERT_FALSE(side_query_is_trigger("/btwmore"));
    return 0;
}

int test_side_query_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("side_query");
    RUN_TEST(test_trigger_exact);
    RUN_TEST(test_extract_question);
    RUN_TEST(test_trigger_word_boundary);
    return _t_failed;
}
