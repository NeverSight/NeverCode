#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_init(void) {
    session_index_init();
    ASSERT_INT_EQ(session_index_count(), 0);
    session_index_free();
    return 0;
}
static int test_add(void) {
    session_index_init();
    ASSERT_INT_EQ(session_index_add("s1", "/tmp/ws", "gpt-5.4"), 0);
    ASSERT_INT_EQ(session_index_count(), 1);
    ASSERT_INT_EQ(session_index_active_count(), 1);
    session_index_free();
    return 0;
}
static int test_no_dup(void) {
    session_index_init();
    session_index_add("s1", "/tmp", "m1");
    ASSERT_INT_EQ(session_index_add("s1", "/tmp", "m1"), -1);
    ASSERT_INT_EQ(session_index_count(), 1);
    session_index_free();
    return 0;
}
static int test_archive(void) {
    session_index_init();
    session_index_add("s1", "/tmp", "m1");
    session_index_set_archived("s1", true);
    ASSERT_INT_EQ(session_index_active_count(), 0);
    ASSERT_INT_EQ(session_index_count(), 1);
    session_index_free();
    return 0;
}
static int test_update(void) {
    session_index_init();
    session_index_add("s1", "/tmp", "m1");
    session_index_update_activity("s1", 10, 1.5);
    session_index_free();
    return 0;
}
static int test_serialize(void) {
    session_index_init();
    session_index_add("s1", "/tmp/ws", "gpt-5.4");
    session_index_update_activity("s1", 5, 0.25);
    char *json = session_index_serialize();
    ASSERT_NOT_NULL(json);
    session_index_free();
    session_index_init();
    ASSERT(session_index_deserialize(json) > 0);
    ASSERT_INT_EQ(session_index_count(), 1);
    free(json);
    session_index_free();
    return 0;
}
static int test_format(void) {
    session_index_init();
    session_index_add("s1", "/tmp", "m1");
    char *s = session_index_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "s1");
    free(s);
    session_index_free();
    return 0;
}

int test_rollout_index_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("rollout_index");
    RUN_TEST(test_init);
    RUN_TEST(test_add);
    RUN_TEST(test_no_dup);
    RUN_TEST(test_archive);
    RUN_TEST(test_update);
    RUN_TEST(test_serialize);
    RUN_TEST(test_format);
    TEST_RETURN_FAILED();
}
