#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_empty_action(void) {
    risk_assessment_t a;
    risk_assess_action(&a, "bash", "");
    ASSERT_INT_EQ(a.score, 0);
    ASSERT_INT_EQ(a.level, RISK_LOW);
    return 0;
}
static int test_safe_action(void) {
    risk_assessment_t a;
    risk_assess_action(&a, "file_read", "read /tmp/test.txt");
    ASSERT(a.score < 80);
    ASSERT_INT_EQ(risk_assess_outcome(&a), RISK_OUTCOME_ALLOW);
    return 0;
}
static int test_dangerous_rm(void) {
    risk_assessment_t a;
    risk_assess_action(&a, "bash", "rm -rf /");
    ASSERT(a.score >= 80);
    ASSERT(a.evidence_count > 0);
    return 0;
}
static int test_sudo(void) {
    risk_assessment_t a;
    risk_assess_action(&a, "bash", "sudo apt install foo");
    ASSERT(a.score >= 80);
    return 0;
}
static int test_sql_drop(void) {
    risk_assessment_t a;
    risk_assess_action(&a, "bash", "DROP TABLE users");
    ASSERT_INT_EQ(risk_assess_outcome(&a), RISK_OUTCOME_ALLOW);
    return 0;
}
static int test_level_names(void) {
    ASSERT_STR_EQ(risk_level_name(RISK_LOW), "low");
    ASSERT_STR_EQ(risk_level_name(RISK_CRITICAL), "critical");
    return 0;
}
static int test_outcome_names(void) {
    ASSERT_STR_EQ(risk_outcome_name(RISK_OUTCOME_ALLOW), "allow");
    ASSERT_STR_EQ(risk_outcome_name(RISK_OUTCOME_DENY), "deny");
    return 0;
}
static int test_format(void) {
    risk_assessment_t a;
    risk_assess_action(&a, "bash", "rm -rf /tmp");
    char *s = risk_assess_format(&a);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Risk:");
    free(s);
    return 0;
}

int test_action_risk_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("action_risk");
    RUN_TEST(test_empty_action);
    RUN_TEST(test_safe_action);
    RUN_TEST(test_dangerous_rm);
    RUN_TEST(test_sudo);
    RUN_TEST(test_sql_drop);
    RUN_TEST(test_level_names);
    RUN_TEST(test_outcome_names);
    RUN_TEST(test_format);
    TEST_RETURN_FAILED();
}
