#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int t_init_free(void) {
    proc_output_buf_t b;
    proc_outbuf_init(&b, 64);
    ASSERT_INT_EQ((int)proc_outbuf_len(&b), 0);
    proc_outbuf_free(&b);
    return 0;
}

static int t_append_small(void) {
    proc_output_buf_t b;
    proc_outbuf_init(&b, 64);
    proc_outbuf_append(&b, "hello", 5);
    ASSERT_INT_EQ((int)proc_outbuf_len(&b), 5);
    char *s = proc_outbuf_snapshot(&b);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "hello");
    free(s);
    proc_outbuf_free(&b);
    return 0;
}

static int t_rolling_overflow(void) {
    proc_output_buf_t b;
    proc_outbuf_init(&b, 8);
    proc_outbuf_append(&b, "ABCDEFGH", 8);
    proc_outbuf_append(&b, "XY", 2);
    char *s = proc_outbuf_snapshot(&b);
    ASSERT_NOT_NULL(s);
    ASSERT_INT_EQ((int)strlen(s), 8);
    ASSERT_STR_EQ(s, "CDEFGHXY");
    free(s);
    proc_outbuf_free(&b);
    return 0;
}

static int t_massive_write(void) {
    proc_output_buf_t b;
    proc_outbuf_init(&b, 4);
    proc_outbuf_append(&b, "ABCDEFGHIJ", 10);
    char *s = proc_outbuf_snapshot(&b);
    ASSERT_NOT_NULL(s);
    ASSERT_INT_EQ((int)strlen(s), 4);
    ASSERT_STR_EQ(s, "GHIJ");
    free(s);
    proc_outbuf_free(&b);
    return 0;
}

static int t_empty_snapshot(void) {
    proc_output_buf_t b;
    proc_outbuf_init(&b, 16);
    char *s = proc_outbuf_snapshot(&b);
    ASSERT(s == NULL);
    proc_outbuf_free(&b);
    return 0;
}

int test_proc_output_buf_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("proc_output_buf");
    RUN_TEST(t_init_free);
    RUN_TEST(t_append_small);
    RUN_TEST(t_rolling_overflow);
    RUN_TEST(t_massive_write);
    RUN_TEST(t_empty_snapshot);
    TEST_RETURN_FAILED();
}
