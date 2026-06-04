/*
 * test_handoff.c — Tests for context handoff serialization and resume prompts.
 */

#include "test.h"
#include "harness.h"
#include "defaults.h"

static int test_init(void) {
    handoff_state_t h;
    handoff_state_init(&h);
    ASSERT_NULL(h.goal_summary);
    ASSERT_NULL(h.decisions_made);
    ASSERT_INT_EQ(h.turn_count, 0);
    handoff_state_free(&h);
    return 0;
}

static int test_serialize_empty(void) {
    handoff_state_t h;
    handoff_state_init(&h);
    char *json = handoff_state_serialize(&h);
    ASSERT_NOT_NULL(json);
    ASSERT(strlen(json) > 0);
    free(json);
    handoff_state_free(&h);
    return 0;
}

static int test_roundtrip(void) {
    handoff_state_t h;
    handoff_state_init(&h);
    h.goal_summary = nc_xstrdup("fix all bugs");
    h.decisions_made = nc_xstrdup("chose quick fix");
    h.pending_tasks = nc_xstrdup("run tests");
    h.files_modified = nc_xstrdup("src/main.c");
    h.key_findings = nc_xstrdup("root cause found");
    h.errors_encountered = nc_xstrdup("timeout on API call 7");
    h.working_branch = nc_xstrdup("feature/hotfix");
    h.dirty_files = 3;
    h.turn_count = 42;
    h.handoff_count = 3;
    h.elapsed_s = 1200;

    char *json = handoff_state_serialize(&h);
    ASSERT_NOT_NULL(json);

    handoff_state_t h2;
    handoff_state_init(&h2);
    int rc = handoff_state_deserialize(&h2, json);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_STR_EQ(h2.goal_summary, "fix all bugs");
    ASSERT_STR_EQ(h2.decisions_made, "chose quick fix");
    ASSERT_STR_EQ(h2.pending_tasks, "run tests");
    ASSERT_STR_EQ(h2.files_modified, "src/main.c");
    ASSERT_STR_EQ(h2.key_findings, "root cause found");
    ASSERT_STR_EQ(h2.errors_encountered, "timeout on API call 7");
    ASSERT_STR_EQ(h2.working_branch, "feature/hotfix");
    ASSERT_INT_EQ(h2.dirty_files, 3);
    ASSERT_INT_EQ(h2.turn_count, 42);
    ASSERT_INT_EQ(h2.handoff_count, 3);

    free(json);
    handoff_state_free(&h);
    handoff_state_free(&h2);
    return 0;
}

static int test_deserialize_null(void) {
    handoff_state_t h;
    handoff_state_init(&h);
    ASSERT_INT_EQ(handoff_state_deserialize(&h, NULL), -1);
    ASSERT_INT_EQ(handoff_state_deserialize(&h, "not json"), -1);
    handoff_state_free(&h);
    return 0;
}

static int test_resume_prompt(void) {
    handoff_state_t h;
    handoff_state_init(&h);
    h.goal_summary = nc_xstrdup("deploy v2");
    h.pending_tasks = nc_xstrdup("run integration tests");
    h.turn_count = 10;
    h.handoff_count = 1;
    h.elapsed_s = 600;

    char *prompt = handoff_build_resume_prompt(&h);
    ASSERT_NOT_NULL(prompt);
    ASSERT_STR_CONTAINS(prompt, "deploy v2");
    ASSERT_STR_CONTAINS(prompt, "integration tests");
    ASSERT_STR_CONTAINS(prompt, "10 turns");
    ASSERT_STR_CONTAINS(prompt, "Context compacted");

    free(prompt);
    handoff_state_free(&h);
    return 0;
}

static int test_resume_prompt_empty(void) {
    handoff_state_t h;
    handoff_state_init(&h);
    char *prompt = handoff_build_resume_prompt(&h);
    ASSERT_NOT_NULL(prompt);
    ASSERT_STR_CONTAINS(prompt, "Context compacted");
    free(prompt);
    handoff_state_free(&h);
    return 0;
}

static int test_resume_prompt_with_errors_and_branch(void) {
    handoff_state_t h;
    handoff_state_init(&h);
    h.goal_summary = nc_xstrdup("refactor API layer");
    h.errors_encountered = nc_xstrdup("429 rate limit on turn 5");
    h.working_branch = nc_xstrdup("main");
    h.dirty_files = 2;
    h.turn_count = 15;
    h.handoff_count = 1;
    h.elapsed_s = 900;

    char *prompt = handoff_build_resume_prompt(&h);
    ASSERT_NOT_NULL(prompt);
    ASSERT_STR_CONTAINS(prompt, "Errors Encountered");
    ASSERT_STR_CONTAINS(prompt, "429 rate limit");
    ASSERT_STR_CONTAINS(prompt, "Branch: main");
    ASSERT_STR_CONTAINS(prompt, "Dirty files: 2");

    free(prompt);
    handoff_state_free(&h);
    return 0;
}

int test_handoff_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("handoff");
    RUN_TEST(test_init);
    RUN_TEST(test_serialize_empty);
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_deserialize_null);
    RUN_TEST(test_resume_prompt);
    RUN_TEST(test_resume_prompt_empty);
    RUN_TEST(test_resume_prompt_with_errors_and_branch);
    TEST_RETURN_FAILED();
}
