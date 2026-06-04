/*
 * test_sprint_contract.c — Tests for adversarial evaluation contract system.
 */

#include "test.h"
#include "harness.h"

static int t_create_and_count(void) {
    sprint_contract_init();
    ASSERT_INT_EQ(sprint_contract_count(), 0);
    int idx = sprint_contract_create("Build login page", 3);
    ASSERT_INT_EQ(idx, 0);
    ASSERT_INT_EQ(sprint_contract_count(), 1);
    ASSERT_INT_EQ(sprint_contract_active(), 0);
    sprint_contract_free();
    return 0;
}

static int t_add_criteria(void) {
    sprint_contract_init();
    int idx = sprint_contract_create("Implement API", 5);
    ASSERT_INT_EQ(sprint_contract_add_criterion(idx, "Returns 200 on valid request"), 0);
    ASSERT_INT_EQ(sprint_contract_add_criterion(idx, "Returns 400 on invalid input"), 1);
    ASSERT_INT_EQ(sprint_contract_add_criterion(idx, "Response time < 100ms"), 2);
    ASSERT_INT_EQ(sprint_contract_criteria_count(idx), 3);
    sprint_contract_free();
    return 0;
}

static int t_grade_and_iterate(void) {
    sprint_contract_init();
    int idx = sprint_contract_create("Build widget", 3);
    sprint_contract_add_criterion(idx, "Renders correctly");
    sprint_contract_add_criterion(idx, "Handles click events");

    sprint_contract_grade(idx, 0, true, NULL);
    sprint_contract_grade(idx, 1, false, "Click handler not wired");
    sprint_contract_iterate(idx);

    ASSERT_INT_EQ(sprint_contract_pass_count(idx), 1);
    ASSERT_INT_EQ(sprint_contract_fail_count(idx), 1);
    ASSERT_FALSE(sprint_contract_finalized(idx));
    ASSERT_FALSE(sprint_contract_all_pass(idx));
    ASSERT_INT_EQ(sprint_contract_iteration(idx), 1);

    sprint_contract_grade(idx, 1, true, NULL);
    sprint_contract_iterate(idx);

    ASSERT_INT_EQ(sprint_contract_pass_count(idx), 2);
    ASSERT_INT_EQ(sprint_contract_fail_count(idx), 0);
    ASSERT_TRUE(sprint_contract_finalized(idx));
    ASSERT_TRUE(sprint_contract_all_pass(idx));

    sprint_contract_free();
    return 0;
}

static int t_max_iterations_cap(void) {
    sprint_contract_init();
    int idx = sprint_contract_create("Hard task", 2);
    sprint_contract_add_criterion(idx, "Never passes");

    sprint_contract_grade(idx, 0, false, "still broken");
    sprint_contract_iterate(idx);
    ASSERT_FALSE(sprint_contract_finalized(idx));

    sprint_contract_grade(idx, 0, false, "still broken");
    sprint_contract_iterate(idx);
    ASSERT_TRUE(sprint_contract_finalized(idx));

    int rc = sprint_contract_iterate(idx);
    ASSERT_INT_EQ(rc, -1);

    sprint_contract_free();
    return 0;
}

static int t_build_feedback(void) {
    sprint_contract_init();
    int idx = sprint_contract_create("Test sprint", 3);
    sprint_contract_add_criterion(idx, "Unit tests pass");
    sprint_contract_add_criterion(idx, "No lint errors");

    sprint_contract_grade(idx, 0, true, NULL);
    sprint_contract_grade(idx, 1, false, "3 warnings found");
    sprint_contract_iterate(idx);

    char *fb = sprint_contract_build_feedback(idx);
    ASSERT_NOT_NULL(fb);
    ASSERT_STR_CONTAINS(fb, "[PASS] Unit tests pass");
    ASSERT_STR_CONTAINS(fb, "[FAIL] No lint errors");
    ASSERT_STR_CONTAINS(fb, "3 warnings found");
    ASSERT_STR_CONTAINS(fb, "1/2 criteria passing");
    free(fb);

    sprint_contract_free();
    return 0;
}

static int t_build_prompt(void) {
    sprint_contract_init();
    int idx = sprint_contract_create("Build dashboard", 5);
    sprint_contract_add_criterion(idx, "Charts render");
    sprint_contract_add_criterion(idx, "Data loads from API");

    char *prompt = sprint_contract_build_prompt(idx);
    ASSERT_NOT_NULL(prompt);
    ASSERT_STR_CONTAINS(prompt, "Build dashboard");
    ASSERT_STR_CONTAINS(prompt, "Charts render");
    ASSERT_STR_CONTAINS(prompt, "Data loads from API");
    free(prompt);

    sprint_contract_grade(idx, 0, false, "chart library not imported");
    sprint_contract_iterate(idx);

    char *prompt2 = sprint_contract_build_prompt(idx);
    ASSERT_NOT_NULL(prompt2);
    ASSERT_STR_CONTAINS(prompt2, "iteration 1");
    ASSERT_STR_CONTAINS(prompt2, "chart library not imported");
    free(prompt2);

    sprint_contract_free();
    return 0;
}

static int t_format_status(void) {
    sprint_contract_init();
    sprint_contract_create("Sprint A", 3);
    sprint_contract_add_criterion(0, "Criterion 1");
    sprint_contract_grade(0, 0, true, NULL);
    sprint_contract_iterate(0);

    char *status = sprint_contract_format_status();
    ASSERT_NOT_NULL(status);
    ASSERT_STR_CONTAINS(status, "Sprint A");
    ASSERT_STR_CONTAINS(status, "Contracts: 1");
    free(status);

    sprint_contract_free();
    return 0;
}

static int t_multiple_contracts(void) {
    sprint_contract_init();
    int a = sprint_contract_create("Sprint 1", 3);
    int b = sprint_contract_create("Sprint 2", 3);
    ASSERT_INT_EQ(a, 0);
    ASSERT_INT_EQ(b, 1);
    ASSERT_INT_EQ(sprint_contract_count(), 2);
    ASSERT_INT_EQ(sprint_contract_active(), 1);
    sprint_contract_free();
    return 0;
}

static int t_null_safety(void) {
    sprint_contract_init();
    ASSERT_INT_EQ(sprint_contract_create(NULL, 3), -1);
    ASSERT_INT_EQ(sprint_contract_add_criterion(-1, "x"), -1);
    ASSERT_INT_EQ(sprint_contract_add_criterion(0, NULL), -1);
    ASSERT_INT_EQ(sprint_contract_grade(-1, 0, true, NULL), -1);
    ASSERT_FALSE(sprint_contract_all_pass(-1));
    ASSERT_FALSE(sprint_contract_finalized(-1));
    ASSERT_NULL(sprint_contract_build_feedback(-1));
    ASSERT_NULL(sprint_contract_build_prompt(-1));
    sprint_contract_free();
    return 0;
}

static int t_macros(void) {
    ASSERT_GT(SPRINT_MAX_CONTRACTS, 0);
    ASSERT_GT(SPRINT_MAX_CRITERIA, 0);
    ASSERT_GT(SPRINT_DESC_MAX, 0);
    ASSERT_GT(SPRINT_CRITERION_MAX, 0);
    ASSERT_GT(SPRINT_FEEDBACK_MAX, 0);
    ASSERT_GT(SPRINT_DEFAULT_ITERATIONS, 0);
    return 0;
}

int test_sprint_contract_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("sprint_contract");
    RUN_TEST(t_create_and_count);
    RUN_TEST(t_add_criteria);
    RUN_TEST(t_grade_and_iterate);
    RUN_TEST(t_max_iterations_cap);
    RUN_TEST(t_build_feedback);
    RUN_TEST(t_build_prompt);
    RUN_TEST(t_format_status);
    RUN_TEST(t_multiple_contracts);
    RUN_TEST(t_null_safety);
    RUN_TEST(t_macros);
    TEST_RETURN_FAILED();
}
