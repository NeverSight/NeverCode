#include "test.h"
#include "harness.h"
#include "nevercode.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Failure Classification ── */

static int test_classify_transient(void) {
    ASSERT_INT_EQ(harness_classify_failure("rate_limit exceeded", 429), FAIL_TRANSIENT);
    ASSERT_INT_EQ(harness_classify_failure("server overloaded", 503), FAIL_TRANSIENT);
    ASSERT_INT_EQ(harness_classify_failure("overloaded", 529), FAIL_TRANSIENT);
    return 0;
}

static int test_classify_resource(void) {
    ASSERT_INT_EQ(harness_classify_failure("prompt_too_long", 400), FAIL_RESOURCE);
    return 0;
}

static int test_classify_fatal(void) {
    ASSERT_INT_EQ(harness_classify_failure("invalid auth key", 401), FAIL_FATAL);
    ASSERT_INT_EQ(harness_classify_failure(NULL, 0), FAIL_FATAL);
    return 0;
}

static int test_classify_persistent(void) {
    ASSERT_INT_EQ(harness_classify_failure("internal server error", 500), FAIL_PERSISTENT);
    return 0;
}

static int test_classify_semantic(void) {
    ASSERT_INT_EQ(harness_classify_failure("invalid request body", 400), FAIL_SEMANTIC);
    return 0;
}

static int test_failure_name(void) {
    ASSERT_STR_EQ(harness_failure_name(FAIL_TRANSIENT), "transient");
    ASSERT_STR_EQ(harness_failure_name(FAIL_PERSISTENT), "persistent");
    ASSERT_STR_EQ(harness_failure_name(FAIL_RESOURCE), "resource");
    ASSERT_STR_EQ(harness_failure_name(FAIL_SEMANTIC), "semantic");
    ASSERT_STR_EQ(harness_failure_name(FAIL_FATAL), "fatal");
    return 0;
}

/* ── Retry Logic ── */

static int test_should_retry(void) {
    ASSERT_TRUE(harness_should_retry(FAIL_TRANSIENT, 0));
    ASSERT_FALSE(harness_should_retry(FAIL_TRANSIENT, 5));
    ASSERT_FALSE(harness_should_retry(FAIL_FATAL, 0));
    ASSERT_TRUE(harness_should_retry(FAIL_RESOURCE, 0));
    return 0;
}

static int test_retry_delay(void) {
    int d1 = harness_retry_delay_ms(FAIL_TRANSIENT, 0);
    int d2 = harness_retry_delay_ms(FAIL_TRANSIENT, 1);
    ASSERT(d1 > 0);
    ASSERT(d2 > d1);
    ASSERT_INT_EQ(harness_retry_delay_ms(FAIL_FATAL, 0), 0);
    return 0;
}

static int test_retry_delay_capped(void) {
    int d = harness_retry_delay_ms(FAIL_TRANSIENT, 20);
    ASSERT(d <= HARNESS_RETRY_MAX_DELAY);
    return 0;
}

/* ── Goal Tracker ── */

static int test_goal_basic(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    goal_tracker_push(&g, "task", 1);
    ASSERT_STR_EQ(goal_tracker_current(&g), "task");
    ASSERT_INT_EQ(goal_tracker_pending_count(&g), 1);
    goal_tracker_complete(&g, 0, "done");
    ASSERT_INT_EQ(goal_tracker_completed_count(&g), 1);
    ASSERT_INT_EQ(goal_tracker_pending_count(&g), 0);
    goal_tracker_free(&g);
    return 0;
}

static int test_goal_fail(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    goal_tracker_push(&g, "A", 1);
    goal_tracker_push(&g, "B", 2);
    goal_tracker_fail(&g, 0, "error");
    ASSERT(g.active_idx == 1);
    goal_tracker_free(&g);
    return 0;
}

static int test_goal_format(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    goal_tracker_push(&g, "format_test", 1);
    char *s = goal_tracker_format_status(&g);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "format_test");
    free(s);
    goal_tracker_free(&g);
    return 0;
}

static int test_goal_status_names(void) {
    ASSERT_STR_EQ(goal_status_name(GOAL_PENDING), "pending");
    ASSERT_STR_EQ(goal_status_name(GOAL_ACTIVE), "active");
    ASSERT_STR_EQ(goal_status_name(GOAL_COMPLETED), "completed");
    ASSERT_STR_EQ(goal_status_name(GOAL_FAILED), "failed");
    ASSERT_STR_EQ(goal_status_name(GOAL_EVOLVED), "evolved");
    return 0;
}

static int test_goal_empty(void) {
    goal_tracker_t g;
    goal_tracker_init(&g);
    ASSERT_NULL(goal_tracker_current(&g));
    ASSERT_INT_EQ(goal_tracker_pending_count(&g), 0);
    ASSERT_INT_EQ(goal_tracker_completed_count(&g), 0);
    goal_tracker_free(&g);
    return 0;
}

/* ── Handoff ── */

static int test_handoff_serde(void) {
    handoff_state_t h;
    handoff_state_init(&h);
    h.goal_summary = nc_xstrdup("refactor module");
    h.turn_count = 42;
    h.handoff_count = 2;

    char *json = handoff_state_serialize(&h);
    ASSERT_NOT_NULL(json);

    handoff_state_t h2;
    handoff_state_init(&h2);
    ASSERT_INT_EQ(handoff_state_deserialize(&h2, json), 0);
    ASSERT_STR_EQ(h2.goal_summary, "refactor module");
    ASSERT_INT_EQ(h2.turn_count, 42);

    free(json);
    handoff_state_free(&h);
    handoff_state_free(&h2);
    return 0;
}

static int test_handoff_prompt(void) {
    handoff_state_t h;
    handoff_state_init(&h);
    h.goal_summary = nc_xstrdup("fix the bug");
    h.pending_tasks = nc_xstrdup("write tests");
    h.turn_count = 10;

    char *p = handoff_build_resume_prompt(&h);
    ASSERT_NOT_NULL(p);
    ASSERT_STR_CONTAINS(p, "fix the bug");
    ASSERT_STR_CONTAINS(p, "10 turns");

    free(p);
    handoff_state_free(&h);
    return 0;
}

static int test_handoff_empty(void) {
    handoff_state_t h;
    handoff_state_init(&h);
    char *json = handoff_state_serialize(&h);
    ASSERT_NOT_NULL(json);
    free(json);
    handoff_state_free(&h);
    return 0;
}

/* ── Daemon ── */

static int test_daemon_lifecycle(void) {
    daemon_ctx_t d;
    daemon_ctx_init(&d);
    ASSERT_FALSE(daemon_is_running(&d));

    daemon_record_turn(&d);
    ASSERT_TRUE(daemon_is_running(&d));
    ASSERT_INT_EQ(d.total_turns, 1);

    daemon_record_failure(&d, FAIL_TRANSIENT, "timeout");
    ASSERT_INT_EQ(d.fail.consecutive, 1);
    ASSERT_INT_EQ(d.total_failures, 1);

    daemon_record_turn(&d);
    ASSERT_INT_EQ(d.fail.consecutive, 0);
    ASSERT_INT_EQ(d.total_turns, 2);

    daemon_ctx_free(&d);
    return 0;
}

static int test_daemon_format(void) {
    daemon_ctx_t d;
    daemon_ctx_init(&d);
    d.state = DAEMON_RUNNING;
    d.total_turns = 50;
    char *s = daemon_format_status(&d);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "running");
    free(s);
    daemon_ctx_free(&d);
    return 0;
}

static int test_daemon_handoff_count(void) {
    daemon_ctx_t d;
    daemon_ctx_init(&d);
    daemon_record_handoff(&d);
    daemon_record_handoff(&d);
    ASSERT_INT_EQ(d.total_handoffs, 2);
    daemon_ctx_free(&d);
    return 0;
}

/* ── Harness Controller ── */

static int test_harness_safe_zone(void) {
    harness_ctx_t h;
    harness_init(&h);
    harness_post_turn(&h, 50, 3, false);
    ASSERT_FALSE(harness_needs_handoff(&h));
    harness_free(&h);
    return 0;
}

static int test_harness_exhaustion_trigger(void) {
    harness_ctx_t h;
    harness_init(&h);
    harness_post_turn(&h, 75, 2, false);
    ASSERT_TRUE(harness_needs_handoff(&h));
    harness_free(&h);
    return 0;
}

static int test_harness_global(void) {
    harness_ctx_t *g = harness_global();
    ASSERT_NOT_NULL(g);
    harness_init(g);
    ASSERT_FALSE(harness_needs_handoff(g));
    harness_free(g);
    return 0;
}

static int test_checkpoint_roundtrip_isolated_home(void) {
    char tmp[256];
    char agents[288];
    snprintf(tmp, sizeof(tmp), "/tmp/nevercode_ckpt_%d", (int)getpid());
    snprintf(agents, sizeof(agents), "%s/.agents", tmp);

    const char *old_home = getenv(ENV_HOME);
    char old_buf[4096];
    if (old_home)
        nc_strlcpy(old_buf, old_home, sizeof(old_buf));

    ASSERT_INT_EQ(mkdir(tmp, 0755), 0);
    ASSERT_INT_EQ(mkdir(agents, 0755), 0);
    ASSERT_INT_EQ(setenv("HOME", tmp, 1), 0);
    config_home_reset_cache();

    handoff_state_t st;
    handoff_state_init(&st);
    st.goal_summary = nc_xstrdup("checkpoint-goal");
    st.turn_count = 7;
    ASSERT_INT_EQ(harness_checkpoint_save("ckpt_sess", 42, &st), 0);
    handoff_state_free(&st);

    handoff_state_t ld;
    handoff_state_init(&ld);
    int turn = -1;
    ASSERT_INT_EQ(harness_checkpoint_load("ckpt_sess", &ld, &turn), 0);
    ASSERT_INT_EQ(turn, 42);
    ASSERT_NOT_NULL(ld.goal_summary);
    ASSERT_STR_EQ(ld.goal_summary, "checkpoint-goal");
    handoff_state_free(&ld);

    harness_checkpoint_cleanup("ckpt_sess", 0);

    char cpdir[320];
    snprintf(cpdir, sizeof(cpdir), "%s/checkpoints", agents);
    /* best-effort remove dirs */
    (void)rmdir(cpdir);
    (void)rmdir(agents);
    (void)rmdir(tmp);

    if (old_home)
        ASSERT_INT_EQ(setenv("HOME", old_buf, 1), 0);
    else
        (void)unsetenv("HOME");
    config_home_reset_cache();
    return 0;
}

int test_harness_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("harness");

    RUN_TEST(test_classify_transient);
    RUN_TEST(test_classify_resource);
    RUN_TEST(test_classify_fatal);
    RUN_TEST(test_classify_persistent);
    RUN_TEST(test_classify_semantic);
    RUN_TEST(test_failure_name);
    RUN_TEST(test_should_retry);
    RUN_TEST(test_retry_delay);
    RUN_TEST(test_retry_delay_capped);
    RUN_TEST(test_goal_basic);
    RUN_TEST(test_goal_fail);
    RUN_TEST(test_goal_format);
    RUN_TEST(test_goal_status_names);
    RUN_TEST(test_goal_empty);
    RUN_TEST(test_handoff_serde);
    RUN_TEST(test_handoff_prompt);
    RUN_TEST(test_handoff_empty);
    RUN_TEST(test_daemon_lifecycle);
    RUN_TEST(test_daemon_format);
    RUN_TEST(test_daemon_handoff_count);
    RUN_TEST(test_harness_safe_zone);
    RUN_TEST(test_harness_exhaustion_trigger);
    RUN_TEST(test_harness_global);
    RUN_TEST(test_checkpoint_roundtrip_isolated_home);

    TEST_RETURN_FAILED();
}
