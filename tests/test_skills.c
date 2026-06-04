#include "test.h"
#include "nevercode.h"

static int test_skill_find_verify(void) {
    const char *p = skill_find_prompt("verify");
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "verify") != NULL || strstr(p, "test") != NULL);
    return 0;
}

static int test_skill_find_debug(void) {
    const char *p = skill_find_prompt("debug");
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "debug") != NULL || strstr(p, "error") != NULL);
    return 0;
}

static int test_skill_find_simplify(void) {
    const char *p = skill_find_prompt("simplify");
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "simplify") != NULL || strstr(p, "refactor") != NULL);
    return 0;
}

static int test_skill_find_stuck(void) {
    const char *p = skill_find_prompt("stuck");
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "circles") != NULL || strstr(p, "different") != NULL);
    return 0;
}

static int test_skill_find_batch(void) {
    const char *p = skill_find_prompt("batch");
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "files") != NULL || strstr(p, "transformation") != NULL);
    return 0;
}

static int test_skill_find_remember(void) {
    const char *p = skill_find_prompt("remember");
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "AGENTS.md") != NULL);
    return 0;
}

static int test_skill_find_loop(void) {
    const char *p = skill_find_prompt("loop");
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "iteration") != NULL || strstr(p, "loop") != NULL);
    return 0;
}

static int test_skill_find_recon(void) {
    const char *p = skill_find_prompt("recon");
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "reconnaissance") != NULL || strstr(p, "enumerate") != NULL);
    return 0;
}

static int test_skill_find_exploit(void) {
    const char *p = skill_find_prompt("exploit");
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "exploit") != NULL || strstr(p, "vulnerability") != NULL);
    return 0;
}

static int test_skill_not_found(void) {
    ASSERT_NULL(skill_find_prompt("nonexistent_skill"));
    ASSERT_NULL(skill_find_prompt(NULL));
    return 0;
}

static int test_skill_exists(void) {
    ASSERT(skill_exists("verify") == true);
    ASSERT(skill_exists("debug") == true);
    ASSERT(skill_exists("remember") == true);
    ASSERT(skill_exists("loop") == true);
    ASSERT(skill_exists("recon") == true);
    ASSERT(skill_exists("exploit") == true);
    ASSERT(skill_exists("nope") == false);
    return 0;
}

static int test_skill_list(void) {
    char *listing = NULL;
    skill_list_all(&listing);
    ASSERT_NOT_NULL(listing);
    ASSERT(strstr(listing, "verify") != NULL);
    ASSERT(strstr(listing, "debug") != NULL);
    ASSERT(strstr(listing, "simplify") != NULL);
    ASSERT(strstr(listing, "stuck") != NULL);
    ASSERT(strstr(listing, "batch") != NULL);
    ASSERT(strstr(listing, "remember") != NULL);
    ASSERT(strstr(listing, "loop") != NULL);
    ASSERT(strstr(listing, "recon") != NULL);
    ASSERT(strstr(listing, "exploit") != NULL);
    free(listing);
    return 0;
}

int test_skills_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("skills");
    RUN_TEST(test_skill_find_verify);
    RUN_TEST(test_skill_find_debug);
    RUN_TEST(test_skill_find_simplify);
    RUN_TEST(test_skill_find_stuck);
    RUN_TEST(test_skill_find_batch);
    RUN_TEST(test_skill_find_remember);
    RUN_TEST(test_skill_find_loop);
    RUN_TEST(test_skill_find_recon);
    RUN_TEST(test_skill_find_exploit);
    RUN_TEST(test_skill_not_found);
    RUN_TEST(test_skill_exists);
    RUN_TEST(test_skill_list);
    TEST_RETURN_FAILED();
}
