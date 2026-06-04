#include "test.h"
#include <stdbool.h>
extern void coordinator_mode_set(bool on);
extern bool coordinator_mode_active(void);
extern char *coordinator_build_system_prompt(void);
extern char *coordinator_build_worker_context(int, const char **, const char *);
static int t1(void) {
    coordinator_mode_set(false);
    ASSERT_TRUE(!coordinator_mode_active());
    coordinator_mode_set(true);
    ASSERT_TRUE(coordinator_mode_active());
    coordinator_mode_set(false);
    return 0;
}
static int t2(void) {
    coordinator_mode_set(false);
    ASSERT_NULL(coordinator_build_system_prompt());
    return 0;
}
static int t3(void) {
    coordinator_mode_set(true);
    char *p = coordinator_build_system_prompt();
    ASSERT_NOT_NULL(p);
    ASSERT_TRUE(strstr(p, "coordinator") != NULL);
    ASSERT_TRUE(strstr(p, "TaskCreate") != NULL);
    free(p);
    coordinator_mode_set(false);
    return 0;
}
static int t4(void) {
    coordinator_mode_set(true);
    const char *s[] = {"github", "fs"};
    char *c = coordinator_build_worker_context(2, s, "/tmp/sp");
    ASSERT_NOT_NULL(c);
    ASSERT_TRUE(strstr(c, "github") != NULL);
    ASSERT_TRUE(strstr(c, "Scratchpad") != NULL);
    free(c);
    coordinator_mode_set(false);
    return 0;
}
int test_coordinator_prompt_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("coordinator_prompt");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    TEST_RETURN_FAILED();
}
