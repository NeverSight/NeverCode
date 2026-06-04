#include "test.h"
#include "nevercode.h"
#include "harness.h"
#include <stdio.h>
#include <unistd.h>

static int t_empty_ruleset_passes(void) {
    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    char detail[256] = {0};
    ASSERT_TRUE(sc_evaluate(&rs, "anything", 0, NULL, detail, sizeof(detail)));
    ASSERT_STR_CONTAINS(detail, "no rules");
    return 0;
}

static int t_output_contains(void) {
    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_output_contains(&rs, "hello", true);
    ASSERT_TRUE(sc_evaluate(&rs, "say hello world", 0, NULL, NULL, 0));
    ASSERT_FALSE(sc_evaluate(&rs, "say goodbye", 0, NULL, NULL, 0));
    return 0;
}

static int t_output_contains_case_insensitive(void) {
    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_output_contains(&rs, "hello", false);
    ASSERT_TRUE(sc_evaluate(&rs, "say HELLO world", 0, NULL, NULL, 0));
    return 0;
}

static int t_output_not_contains(void) {
    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_output_not_contains(&rs, "error", true);
    ASSERT_TRUE(sc_evaluate(&rs, "all good", 0, NULL, NULL, 0));
    ASSERT_FALSE(sc_evaluate(&rs, "found error here", 0, NULL, NULL, 0));
    return 0;
}

static int t_exit_zero(void) {
    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_exit_zero(&rs);
    ASSERT_TRUE(sc_evaluate(&rs, "", 0, NULL, NULL, 0));
    ASSERT_FALSE(sc_evaluate(&rs, "", 1, NULL, NULL, 0));
    return 0;
}

static int t_file_exists(void) {
    sc_ruleset_t rs;
    sc_ruleset_init(&rs);

    char tmp[256];
    snprintf(tmp, sizeof(tmp), "/tmp/nc_test_sc_%d.txt", getpid());
    FILE *f = fopen(tmp, "w");
    fputs("data", f);
    fclose(f);

    sc_add_file_exists(&rs, tmp);
    ASSERT_TRUE(sc_evaluate(&rs, "", 0, NULL, NULL, 0));

    unlink(tmp);
    ASSERT_FALSE(sc_evaluate(&rs, "", 0, NULL, NULL, 0));
    return 0;
}

static int t_conjunction_all_must_pass(void) {
    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_output_contains(&rs, "ok", true);
    sc_add_exit_zero(&rs);

    ASSERT_TRUE(sc_evaluate(&rs, "ok", 0, NULL, NULL, 0));
    ASSERT_FALSE(sc_evaluate(&rs, "ok", 1, NULL, NULL, 0));
    ASSERT_FALSE(sc_evaluate(&rs, "nope", 0, NULL, NULL, 0));
    return 0;
}

static int t_max_rules(void) {
    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    for (int i = 0; i < SC_MAX_RULES; i++)
        ASSERT_INT_EQ(sc_add_exit_zero(&rs), 0);
    ASSERT_INT_EQ(sc_add_exit_zero(&rs), -1);
    return 0;
}

static int t_kind_names(void) {
    ASSERT_STR_EQ(sc_kind_name(SC_OUTPUT_CONTAINS), "output_contains");
    ASSERT_STR_EQ(sc_kind_name(SC_FILE_EXISTS), "file_exists");
    ASSERT_STR_EQ(sc_kind_name(SC_EXIT_CODE_ZERO), "exit_zero");
    return 0;
}

int test_success_criteria_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("success_criteria");
    RUN_TEST(t_empty_ruleset_passes);
    RUN_TEST(t_output_contains);
    RUN_TEST(t_output_contains_case_insensitive);
    RUN_TEST(t_output_not_contains);
    RUN_TEST(t_exit_zero);
    RUN_TEST(t_file_exists);
    RUN_TEST(t_conjunction_all_must_pass);
    RUN_TEST(t_max_rules);
    RUN_TEST(t_kind_names);
    TEST_RETURN_FAILED();
}
