#include "test.h"
#include "nevercode.h"
#include <sys/stat.h>

typedef struct ts_search_t ts_search_t;

int transcript_search(const char *home, const char *query, ts_search_t *search);
char *transcript_search_format(const ts_search_t *search);

static char search_buf[65536];

static int test_search_null(void) {
    ts_search_t *s = (ts_search_t *)search_buf;
    ASSERT_INT_EQ(transcript_search(NULL, "test", s), -1);
    ASSERT_INT_EQ(transcript_search("/tmp", NULL, s), -1);
    ASSERT_INT_EQ(transcript_search("/tmp", "test", NULL), -1);
    return 0;
}

static int test_search_no_sessions(void) {
    ts_search_t *s = (ts_search_t *)search_buf;
    int r = transcript_search("/tmp/nevercode_no_sessions_xyz", "test query", s);
    ASSERT_INT_EQ(r, 0);
    return 0;
}

static int test_format_null(void) {
    ASSERT_NULL(transcript_search_format(NULL));
    return 0;
}

static int test_format_empty(void) {
    ts_search_t *s = (ts_search_t *)search_buf;
    memset(s, 0, sizeof(search_buf));
    char *f = transcript_search_format(s);
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "Found 0");
    free(f);
    return 0;
}

int test_transcript_search_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("transcript_search");
    RUN_TEST(test_search_null);
    RUN_TEST(test_search_no_sessions);
    RUN_TEST(test_format_null);
    RUN_TEST(test_format_empty);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("transcript_search");
    RUN_TEST(test_search_null);
    RUN_TEST(test_search_no_sessions);
    RUN_TEST(test_format_null);
    RUN_TEST(test_format_empty);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
