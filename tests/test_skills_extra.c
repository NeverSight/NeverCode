#include "test.h"
#include "nevercode.h"

static int test_skill_security_review(void) {
    const char *p = skill_find_prompt("security-review");
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "security") != NULL);
    ASSERT(strstr(p, "vulnerability") != NULL || strstr(p, "Vulnerability") != NULL);
    ASSERT(strstr(p, "git diff") != NULL);
    return 0;
}

static int test_skill_perf(void) {
    const char *p = skill_find_prompt("perf");
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "performance") != NULL || strstr(p, "bottleneck") != NULL);
    return 0;
}

static int test_skill_test(void) {
    const char *p = skill_find_prompt("test");
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "test") != NULL);
    ASSERT(strstr(p, "edge cases") != NULL);
    return 0;
}

static int test_skill_exists_new(void) {
    ASSERT(skill_exists("security-review") == true);
    ASSERT(skill_exists("perf") == true);
    ASSERT(skill_exists("test") == true);
    ASSERT(skill_exists("nonexistent") == false);
    return 0;
}

static int test_skill_list_includes_new(void) {
    char *listing = NULL;
    skill_list_all(&listing);
    ASSERT_NOT_NULL(listing);
    ASSERT(strstr(listing, "security-review") != NULL);
    ASSERT(strstr(listing, "perf") != NULL);
    ASSERT(strstr(listing, "test") != NULL);
    ASSERT(strstr(listing, "verify") != NULL);
    free(listing);
    return 0;
}

static int test_skill_count_total(void) {
    int count = 0;
    const char *names[] = {"verify",          "debug", "simplify", "stuck", "batch",
                           "security-review", "perf",  "test",     NULL};
    for (int i = 0; names[i]; i++) {
        if (skill_exists(names[i]))
            count++;
    }
    ASSERT_INT_EQ(count, 8);
    return 0;
}

int test_skills_extra_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("skills_extra");
    RUN_TEST(test_skill_security_review);
    RUN_TEST(test_skill_perf);
    RUN_TEST(test_skill_test);
    RUN_TEST(test_skill_exists_new);
    RUN_TEST(test_skill_list_includes_new);
    RUN_TEST(test_skill_count_total);
    TEST_RETURN_FAILED();
}
