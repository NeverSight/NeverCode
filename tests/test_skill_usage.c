/*
 * test_skill_usage.c — Tests for skill usage frequency tracking.
 */
#include "test.h"
#include "nevercode.h"

static int t_empty(void) {
    skill_usage_reset();
    ASSERT_INT_EQ(skill_usage_total(), 0);
    ASSERT_INT_EQ(skill_usage_count("nonexistent"), 0);
    ASSERT(skill_usage_score("nonexistent") == 0.0);
    return 0;
}

static int t_record(void) {
    skill_usage_reset();
    skill_usage_record("commit");
    ASSERT_INT_EQ(skill_usage_total(), 1);
    ASSERT_INT_EQ(skill_usage_count("commit"), 1);
    ASSERT(skill_usage_score("commit") > 0.0);

    skill_usage_record("commit");
    ASSERT_INT_EQ(skill_usage_count("commit"), 2);
    return 0;
}

static int t_multiple_skills(void) {
    skill_usage_reset();
    skill_usage_record("commit");
    skill_usage_record("review");
    skill_usage_record("commit");
    ASSERT_INT_EQ(skill_usage_total(), 2);
    ASSERT_INT_EQ(skill_usage_count("commit"), 2);
    ASSERT_INT_EQ(skill_usage_count("review"), 1);
    ASSERT(skill_usage_score("commit") > skill_usage_score("review"));
    return 0;
}

static int t_null_safety(void) {
    skill_usage_record(NULL);
    skill_usage_record("");
    ASSERT_INT_EQ(skill_usage_count(NULL), 0);
    ASSERT(skill_usage_score(NULL) == 0.0);
    return 0;
}

static int t_macros(void) {
    ASSERT(SKILL_USAGE_MAX > 0);
    ASSERT(SKILL_USAGE_NAME_MAX > 0);
    ASSERT(SKILL_USAGE_HALFLIFE_DAYS > 0.0);
    return 0;
}

int test_skill_usage_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("skill_usage");
    RUN_TEST(t_empty);
    RUN_TEST(t_record);
    RUN_TEST(t_multiple_skills);
    RUN_TEST(t_null_safety);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
