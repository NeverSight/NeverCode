#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

extern void swarm_backend_init(void);
extern void swarm_backend_free(void);
extern int swarm_backend_detect(void);
extern int swarm_backend_count(void);
extern int swarm_backend_available_count(void);
extern int swarm_backend_total_spawned(void);
extern const char *swarm_backend_active_name(void);
extern char *swarm_backend_format(void);

static int test_init_clean(void) {
    swarm_backend_init();
    ASSERT_INT_EQ(0, swarm_backend_count());
    ASSERT_INT_EQ(0, swarm_backend_total_spawned());
    swarm_backend_free();
    return 0;
}

static int test_detect(void) {
    swarm_backend_init();
    int n = swarm_backend_detect();
    ASSERT_GT(n, 0);
    ASSERT_INT_EQ(n, swarm_backend_count());
    ASSERT_GT(swarm_backend_available_count(), 0);
    swarm_backend_free();
    return 0;
}

static int test_active_name(void) {
    swarm_backend_init();
    swarm_backend_detect();
    const char *name = swarm_backend_active_name();
    ASSERT_NOT_NULL(name);
    ASSERT_TRUE(strlen(name) > 0);
    swarm_backend_free();
    return 0;
}

static int test_format(void) {
    swarm_backend_init();
    swarm_backend_detect();
    char *fmt = swarm_backend_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "Swarm backends");
    ASSERT_STR_CONTAINS(fmt, "in-process");
    free(fmt);
    swarm_backend_free();
    return 0;
}

int test_swarm_backend_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("swarm_backend");
    RUN_TEST(test_init_clean);
    RUN_TEST(test_detect);
    RUN_TEST(test_active_name);
    RUN_TEST(test_format);
    return _t_failed;
}

#if 0 // disabled for unified build
#if 0
int main(void)
{
    TEST_COUNTERS;
    TEST_SUITE("swarm_backend");
    RUN_TEST(test_init_clean);
    RUN_TEST(test_detect);
    RUN_TEST(test_active_name);
    RUN_TEST(test_format);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    return _t_failed;
}
#endif
#endif
