#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_init(void) {
    interrupt_ctx_init();
    ASSERT_INT_EQ(interrupt_total_count(), 0);
    interrupt_ctx_free();
    return 0;
}
static int test_record(void) {
    interrupt_ctx_init();
    interrupt_record(5, INTR_USER_CANCEL, 2, true);
    ASSERT_INT_EQ(interrupt_total_count(), 1);
    interrupt_ctx_free();
    return 0;
}
static int test_recent(void) {
    interrupt_ctx_init();
    interrupt_record(10, INTR_TIMEOUT, 0, false);
    ASSERT_TRUE(interrupt_was_recent(5));
    ASSERT_FALSE(interrupt_was_recent(15));
    interrupt_ctx_free();
    return 0;
}
static int test_marker(void) {
    interrupt_ctx_init();
    char *m = interrupt_build_marker(5, INTR_USER_CANCEL);
    ASSERT_NOT_NULL(m);
    ASSERT_STR_CONTAINS(m, "<turn_aborted>");
    ASSERT_STR_CONTAINS(m, "user_cancel");
    free(m);
    interrupt_ctx_free();
    return 0;
}
static int test_reason_names(void) {
    ASSERT_STR_EQ(interrupt_reason_name(INTR_USER_CANCEL), "user_cancel");
    ASSERT_STR_EQ(interrupt_reason_name(INTR_TIMEOUT), "timeout");
    ASSERT_STR_EQ(interrupt_reason_name(INTR_COMPACT), "compact");
    return 0;
}
static int test_format(void) {
    interrupt_ctx_init();
    interrupt_record(1, INTR_USER_CANCEL, 1, true);
    interrupt_record(3, INTR_TIMEOUT, 0, false);
    char *s = interrupt_format_history();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Interrupt history");
    free(s);
    interrupt_ctx_free();
    return 0;
}
static int test_overflow(void) {
    interrupt_ctx_init();
    for (int i = 0; i < INTR_HISTORY_MAX + 8; i++)
        interrupt_record(i, INTR_USER_CANCEL, 0, false);
    ASSERT_INT_EQ(interrupt_total_count(), INTR_HISTORY_MAX + 8);
    interrupt_ctx_free();
    return 0;
}

static int test_marker_uses_defaults_tags(void) {
    char *m = interrupt_build_marker(5, INTR_TIMEOUT);
    ASSERT_NOT_NULL(m);
    ASSERT_STR_CONTAINS(m, INTR_ABORT_OPEN);
    ASSERT_STR_CONTAINS(m, INTR_ABORT_CLOSE);
    free(m);
    return 0;
}

int test_interrupted_turn_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("interrupted_turn");
    RUN_TEST(test_init);
    RUN_TEST(test_record);
    RUN_TEST(test_recent);
    RUN_TEST(test_marker);
    RUN_TEST(test_reason_names);
    RUN_TEST(test_format);
    RUN_TEST(test_overflow);
    RUN_TEST(test_marker_uses_defaults_tags);
    TEST_RETURN_FAILED();
}
