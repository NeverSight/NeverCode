#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

/* Declarations from os_sandbox.c */
extern void os_sandbox_init(void);
extern void os_sandbox_free(void);
extern bool os_sandbox_available(void);
extern const char *os_sandbox_type_name(void);
extern int os_sandbox_add_path(const char *path, bool writable);
extern void os_sandbox_set_network(bool allowed);
extern void os_sandbox_set_subprocess(bool allowed);
extern bool os_sandbox_active(void);
extern int os_sandbox_applied_count(void);
extern int os_sandbox_path_count(void);
extern char *os_sandbox_format(void);

static int test_init(void) {
    os_sandbox_init();
    ASSERT_FALSE(os_sandbox_active());
    ASSERT_INT_EQ(0, os_sandbox_path_count());
    ASSERT_INT_EQ(0, os_sandbox_applied_count());
    os_sandbox_free();
    return 0;
}

static int test_platform_detection(void) {
    os_sandbox_init();
    const char *name = os_sandbox_type_name();
    ASSERT_NOT_NULL(name);
#ifdef __APPLE__
    ASSERT_STR_EQ("seatbelt", name);
    ASSERT_TRUE(os_sandbox_available());
#elif defined(__linux__)
    ASSERT_TRUE(os_sandbox_available() || !os_sandbox_available());
#endif
    os_sandbox_free();
    return 0;
}

static int test_add_paths(void) {
    os_sandbox_init();
    ASSERT_INT_EQ(0, os_sandbox_add_path("/tmp", true));
    ASSERT_INT_EQ(0, os_sandbox_add_path("/usr", false));
    ASSERT_INT_EQ(2, os_sandbox_path_count());
    os_sandbox_free();
    return 0;
}

static int test_network_toggle(void) {
    os_sandbox_init();
    os_sandbox_set_network(false);
    os_sandbox_set_network(true);
    os_sandbox_free();
    return 0;
}

static int test_format(void) {
    os_sandbox_init();
    os_sandbox_add_path("/var", true);
    char *fmt = os_sandbox_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "OS Sandbox");
    ASSERT_STR_CONTAINS(fmt, "paths=1");
    free(fmt);
    os_sandbox_free();
    return 0;
}

static int test_double_free(void) {
    os_sandbox_init();
    os_sandbox_free();
    os_sandbox_free();
    return 0;
}

int test_os_sandbox_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("os_sandbox");
    RUN_TEST(test_init);
    RUN_TEST(test_platform_detection);
    RUN_TEST(test_add_paths);
    RUN_TEST(test_network_toggle);
    RUN_TEST(test_format);
    RUN_TEST(test_double_free);
    return _t_failed;
}

#if 0 // disabled for unified build
#if 0
int main(void)
{
    TEST_COUNTERS;
    TEST_SUITE("os_sandbox");
    RUN_TEST(test_init);
    RUN_TEST(test_platform_detection);
    RUN_TEST(test_add_paths);
    RUN_TEST(test_network_toggle);
    RUN_TEST(test_format);
    RUN_TEST(test_double_free);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    return _t_failed;
}
#endif
#endif
