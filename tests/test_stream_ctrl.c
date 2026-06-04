#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

extern void stream_ctrl_init(void);
extern void stream_ctrl_free(void);
extern void stream_ctrl_reset(void);
extern int stream_ctrl_begin(void);
extern int stream_ctrl_feed(const char *data, int len, bool is_final);
extern int stream_ctrl_commit(void);
extern bool stream_ctrl_should_commit(void);
extern const char *stream_ctrl_assembled(void);
extern int stream_ctrl_assembled_len(void);
extern void stream_ctrl_pause(void);
extern void stream_ctrl_resume(void);
extern void stream_ctrl_cancel(void);
extern bool stream_ctrl_complete(void);
extern bool stream_ctrl_active(void);
extern int stream_ctrl_chunk_count(void);
extern int stream_ctrl_total_bytes(void);
extern char *stream_ctrl_format(void);

static int test_init(void) {
    stream_ctrl_init();
    ASSERT_FALSE(stream_ctrl_active());
    ASSERT_FALSE(stream_ctrl_complete());
    ASSERT_INT_EQ(0, stream_ctrl_chunk_count());
    stream_ctrl_free();
    return 0;
}

static int test_basic_stream(void) {
    stream_ctrl_init();
    stream_ctrl_begin();
    ASSERT_TRUE(stream_ctrl_active());

    ASSERT_INT_EQ(0, stream_ctrl_feed("Hello", 5, false));
    ASSERT_INT_EQ(0, stream_ctrl_feed(" World", 6, true));

    ASSERT_TRUE(stream_ctrl_complete());
    ASSERT_INT_EQ(2, stream_ctrl_chunk_count());
    ASSERT_INT_EQ(11, stream_ctrl_total_bytes());
    ASSERT_STR_EQ("Hello World", stream_ctrl_assembled());
    ASSERT_INT_EQ(11, stream_ctrl_assembled_len());

    stream_ctrl_free();
    return 0;
}

static int test_commit(void) {
    stream_ctrl_init();
    stream_ctrl_begin();
    stream_ctrl_feed("A", 1, false);
    stream_ctrl_feed("B", 1, false);

    ASSERT_TRUE(stream_ctrl_should_commit());
    int n = stream_ctrl_commit();
    ASSERT_INT_EQ(2, n);

    stream_ctrl_feed("C", 1, true);
    n = stream_ctrl_commit();
    ASSERT_INT_EQ(1, n);

    stream_ctrl_free();
    return 0;
}

static int test_pause_resume(void) {
    stream_ctrl_init();
    stream_ctrl_begin();
    ASSERT_TRUE(stream_ctrl_active());
    stream_ctrl_pause();
    ASSERT_TRUE(stream_ctrl_active());
    stream_ctrl_resume();
    ASSERT_TRUE(stream_ctrl_active());
    stream_ctrl_free();
    return 0;
}

static int test_cancel(void) {
    stream_ctrl_init();
    stream_ctrl_begin();
    stream_ctrl_feed("X", 1, false);
    stream_ctrl_cancel();
    ASSERT_FALSE(stream_ctrl_active());
    ASSERT_FALSE(stream_ctrl_complete());
    stream_ctrl_free();
    return 0;
}

static int test_format(void) {
    stream_ctrl_init();
    stream_ctrl_begin();
    stream_ctrl_feed("data", 4, true);
    char *fmt = stream_ctrl_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "Stream");
    ASSERT_STR_CONTAINS(fmt, "complete");
    free(fmt);
    stream_ctrl_free();
    return 0;
}

int test_stream_ctrl_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("stream_ctrl");
    RUN_TEST(test_init);
    RUN_TEST(test_basic_stream);
    RUN_TEST(test_commit);
    RUN_TEST(test_pause_resume);
    RUN_TEST(test_cancel);
    RUN_TEST(test_format);
    return _t_failed;
}

#if 0 // disabled for unified build
#if 0
int main(void)
{
    TEST_COUNTERS;
    TEST_SUITE("stream_ctrl");
    RUN_TEST(test_init);
    RUN_TEST(test_basic_stream);
    RUN_TEST(test_commit);
    RUN_TEST(test_pause_resume);
    RUN_TEST(test_cancel);
    RUN_TEST(test_format);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    return _t_failed;
}
#endif
#endif
