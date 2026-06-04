#include "test.h"
#include "nevercode.h"

static int test_init_free(void) {
    transcript_init();
    ASSERT_INT_EQ(0, transcript_entry_count());
    transcript_free();
    return 0;
}

static int test_append_and_get(void) {
    transcript_init();
    ASSERT_INT_EQ(0, transcript_append("user", "hello"));
    ASSERT_INT_EQ(1, transcript_entry_count());
    char *e = transcript_entry_at(0);
    ASSERT_NOT_NULL(e);
    ASSERT_STR_CONTAINS(e, "hello");
    free(e);
    transcript_free();
    return 0;
}

static int test_format_recent(void) {
    transcript_init();
    transcript_append("a", "first");
    transcript_append("b", "second");
    char *s = transcript_format_recent(10);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Transcript");
    free(s);
    transcript_free();
    return 0;
}

static int test_out_of_bounds(void) {
    transcript_init();
    ASSERT_NULL(transcript_entry_at(0));
    ASSERT_NULL(transcript_entry_at(-1));
    transcript_free();
    return 0;
}

static int test_multiple_entries(void) {
    transcript_init();
    for (int i = 0; i < 5; i++)
        transcript_append("r", "msg");
    ASSERT_INT_EQ(5, transcript_entry_count());
    transcript_free();
    return 0;
}

int test_session_transcript_all(void) {
    TEST_SUITE("session_transcript");
    TEST_COUNTERS;
    RUN_TEST(test_init_free);
    RUN_TEST(test_append_and_get);
    RUN_TEST(test_format_recent);
    RUN_TEST(test_out_of_bounds);
    RUN_TEST(test_multiple_entries);
    TEST_RETURN_FAILED();
}
