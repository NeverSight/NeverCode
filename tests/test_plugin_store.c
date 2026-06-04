#include "test.h"
#include "nevercode.h"
#include <sys/stat.h>

int pstore_init(const char *home);
int pstore_load(void);
int pstore_save(void);
int pstore_add(const char *name, const char *version);
int pstore_remove(const char *name);
int pstore_set_enabled(const char *name, int enabled);
int pstore_is_enabled(const char *name);
int pstore_count(void);
int pstore_enabled_count(void);

static int test_init(void) {
    int r = pstore_init("/tmp");
    ASSERT_INT_EQ(r, 0);
    ASSERT_INT_EQ(pstore_count(), 0);
    return 0;
}

static int test_add_remove(void) {
    pstore_init("/tmp");
    ASSERT_INT_EQ(pstore_add("test-plugin", "1.0"), 0);
    ASSERT_INT_EQ(pstore_count(), 1);
    ASSERT_TRUE(pstore_is_enabled("test-plugin"));

    ASSERT_INT_EQ(pstore_add("test-plugin", "1.0"), -1); /* duplicate */
    ASSERT_INT_EQ(pstore_count(), 1);

    ASSERT_INT_EQ(pstore_remove("test-plugin"), 0);
    ASSERT_INT_EQ(pstore_count(), 0);
    return 0;
}

static int test_enable_disable(void) {
    pstore_init("/tmp");
    pstore_add("p1", "1.0");
    pstore_add("p2", "2.0");

    ASSERT_INT_EQ(pstore_enabled_count(), 2);

    pstore_set_enabled("p1", 0);
    ASSERT_FALSE(pstore_is_enabled("p1"));
    ASSERT_INT_EQ(pstore_enabled_count(), 1);

    pstore_set_enabled("p1", 1);
    ASSERT_TRUE(pstore_is_enabled("p1"));
    ASSERT_INT_EQ(pstore_enabled_count(), 2);

    pstore_remove("p1");
    pstore_remove("p2");
    return 0;
}

static int test_null_init(void) {
    ASSERT_INT_EQ(pstore_init(NULL), -1);
    return 0;
}

int test_plugin_store_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("plugin_store");
    RUN_TEST(test_init);
    RUN_TEST(test_add_remove);
    RUN_TEST(test_enable_disable);
    RUN_TEST(test_null_init);
    return _t_failed;
}

#if 0
int main(void) {
    TEST_COUNTERS;
    TEST_SUITE("plugin_store");
    RUN_TEST(test_init);
    RUN_TEST(test_add_remove);
    RUN_TEST(test_enable_disable);
    RUN_TEST(test_null_init);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    TEST_RETURN_FAILED();
}
#endif
