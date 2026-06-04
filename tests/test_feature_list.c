#include "test.h"
#include "nevercode.h"
#include "harness.h"
#include "defaults.h"
#include <string.h>
#include <stdio.h>

static int t_init(void) {
    feature_list_init();
    ASSERT_INT_EQ(feature_list_count(), 0);
    ASSERT_INT_EQ(feature_list_passing_count(), 0);
    ASSERT_INT_EQ(feature_list_failing_count(), 0);
    ASSERT_TRUE(!feature_list_all_passing());
    feature_list_free();
    return 0;
}

static int t_add_and_query(void) {
    feature_list_init();
    int idx = feature_list_add("User can log in", "auth");
    ASSERT_INT_EQ(idx, 0);
    ASSERT_INT_EQ(feature_list_count(), 1);
    ASSERT_INT_EQ(feature_list_failing_count(), 1);
    ASSERT_INT_EQ(feature_list_passing_count(), 0);
    ASSERT_TRUE(!feature_list_is_passing(0));
    ASSERT_STR_CONTAINS(feature_list_description(0), "log in");
    feature_list_add("Dashboard loads", "ui");
    ASSERT_INT_EQ(feature_list_count(), 2);
    ASSERT_INT_EQ(feature_list_failing_count(), 2);
    feature_list_free();
    return 0;
}

static int t_mark_pass_fail(void) {
    feature_list_init();
    feature_list_add("Feature A", NULL);
    feature_list_add("Feature B", "core");
    feature_list_add("Feature C", NULL);
    ASSERT_INT_EQ(feature_list_mark_pass(1), 0);
    ASSERT_TRUE(feature_list_is_passing(1));
    ASSERT_INT_EQ(feature_list_passing_count(), 1);
    ASSERT_INT_EQ(feature_list_failing_count(), 2);
    ASSERT_INT_EQ(feature_list_mark_pass(0), 0);
    ASSERT_INT_EQ(feature_list_mark_pass(2), 0);
    ASSERT_TRUE(feature_list_all_passing());
    ASSERT_INT_EQ(feature_list_mark_fail(1), 0);
    ASSERT_TRUE(!feature_list_all_passing());
    ASSERT_TRUE(!feature_list_is_passing(1));
    feature_list_free();
    return 0;
}

static int t_next_failing(void) {
    feature_list_init();
    feature_list_add("F1", NULL);
    feature_list_add("F2", NULL);
    feature_list_add("F3", NULL);
    feature_list_record_attempt(0);
    feature_list_record_attempt(0);
    feature_list_record_attempt(1);
    int next = feature_list_next_failing();
    ASSERT_INT_EQ(next, 2);
    feature_list_mark_pass(2);
    next = feature_list_next_failing();
    ASSERT_INT_EQ(next, 1);
    feature_list_free();
    return 0;
}

static int t_build_prompt(void) {
    feature_list_init();
    char *empty = feature_list_build_prompt();
    ASSERT_STR_EQ(empty, "");
    free(empty);
    feature_list_add("Login works", "auth");
    feature_list_add("Dashboard loads", "ui");
    feature_list_mark_pass(0);
    char *prompt = feature_list_build_prompt();
    ASSERT_NOT_NULL(prompt);
    ASSERT_STR_CONTAINS(prompt, "<feature_list");
    ASSERT_STR_CONTAINS(prompt, "passing=\"1\"");
    ASSERT_STR_CONTAINS(prompt, "failing=\"1\"");
    ASSERT_STR_CONTAINS(prompt, "Dashboard loads");
    ASSERT_STR_CONTAINS(prompt, "<instruction>");
    free(prompt);
    feature_list_mark_pass(1);
    char *all_pass = feature_list_build_prompt();
    ASSERT_STR_CONTAINS(all_pass, "All features passing");
    free(all_pass);
    feature_list_free();
    return 0;
}

static int t_format(void) {
    feature_list_init();
    feature_list_add("A", "core");
    feature_list_add("B", NULL);
    feature_list_mark_pass(0);
    feature_list_record_attempt(1);
    feature_list_record_attempt(1);
    char *s = feature_list_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "1/2 passing");
    ASSERT_STR_CONTAINS(s, "[PASS]");
    ASSERT_STR_CONTAINS(s, "[FAIL]");
    ASSERT_STR_CONTAINS(s, "2 attempts");
    free(s);
    feature_list_free();
    return 0;
}

static int t_save_load(void) {
    feature_list_init();
    feature_list_add("Alpha feature", "cat1");
    feature_list_add("Beta feature", "cat2");
    feature_list_mark_pass(0);
    feature_list_record_attempt(1);
    ASSERT_INT_EQ(feature_list_save(), 0);
    feature_list_free();
    feature_list_init();
    int n = feature_list_load();
    ASSERT_INT_EQ(n, 2);
    ASSERT_TRUE(feature_list_is_passing(0));
    ASSERT_TRUE(!feature_list_is_passing(1));
    ASSERT_STR_CONTAINS(feature_list_description(0), "Alpha");
    ASSERT_STR_CONTAINS(feature_list_description(1), "Beta");
    remove(FLIST_FILE);
    feature_list_free();
    return 0;
}

static int t_overflow(void) {
    feature_list_init();
    int i;
    for (i = 0; i < FLIST_MAX_ENTRIES; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Feature %d", i);
        ASSERT_TRUE(feature_list_add(buf, NULL) >= 0);
    }
    ASSERT_INT_EQ(feature_list_add("overflow", NULL), -1);
    feature_list_free();
    return 0;
}

static int t_bounds(void) {
    feature_list_init();
    ASSERT_INT_EQ(feature_list_mark_pass(-1), -1);
    ASSERT_INT_EQ(feature_list_mark_pass(0), -1);
    ASSERT_INT_EQ(feature_list_mark_fail(99), -1);
    ASSERT_INT_EQ(feature_list_record_attempt(-1), -1);
    ASSERT_TRUE(feature_list_description(-1) == NULL);
    ASSERT_TRUE(!feature_list_is_passing(-1));
    feature_list_free();
    return 0;
}

int test_feature_list_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("feature_list");
    RUN_TEST(t_init);
    RUN_TEST(t_add_and_query);
    RUN_TEST(t_mark_pass_fail);
    RUN_TEST(t_next_failing);
    RUN_TEST(t_build_prompt);
    RUN_TEST(t_format);
    RUN_TEST(t_save_load);
    RUN_TEST(t_overflow);
    RUN_TEST(t_bounds);
    TEST_RETURN_FAILED();
}
