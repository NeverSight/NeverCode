#include "test.h"
#include "nevercode.h"
#include "harness.h"
#include "defaults.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int t_init(void) {
    session_preflight_init();
    session_preflight_free();
    return 0;
}

static int t_run(void) {
    progress_tracker_init();
    episode_memory_init();
    feature_list_init();
    session_preflight_init();

    preflight_report_t r = session_preflight_run();
    ASSERT_GE((int)r.elapsed_ms, 0);
    ASSERT_TRUE(r.status == PREFLIGHT_OK || r.status == PREFLIGHT_WARN ||
                r.status == PREFLIGHT_DEGRADED);

    session_preflight_free();
    feature_list_free();
    episode_memory_free();
    progress_tracker_free();
    return 0;
}

static int t_run_outside_repo(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-preflight-XXXXXX";
    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    progress_tracker_init();
    episode_memory_init();
    feature_list_init();
    session_preflight_init();

    preflight_report_t r = session_preflight_run();
    ASSERT_TRUE(r.git_clean);
    ASSERT_INT_EQ(r.git_dirty_count, 0);
    ASSERT_STR_EQ(r.git_branch, "");
    ASSERT_TRUE(r.status == PREFLIGHT_OK);

    session_preflight_free();
    feature_list_free();
    episode_memory_free();
    progress_tracker_free();

    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    ASSERT_INT_EQ(rmdir(tmpdir), 0);
    return 0;
}

static int t_format(void) {
    preflight_report_t r;
    memset(&r, 0, sizeof(r));
    r.status = PREFLIGHT_OK;
    r.git_clean = true;
    r.elapsed_ms = 42;
    snprintf(r.git_branch, sizeof(r.git_branch), "main");
    snprintf(r.last_commit, sizeof(r.last_commit), "abc1234 Initial commit");

    char *s = session_preflight_format(&r);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "ok");
    ASSERT_STR_CONTAINS(s, "main");
    ASSERT_STR_CONTAINS(s, "clean");
    free(s);
    return 0;
}

static int t_format_dirty(void) {
    preflight_report_t r;
    memset(&r, 0, sizeof(r));
    r.status = PREFLIGHT_WARN;
    r.git_clean = false;
    r.git_dirty_count = 15;
    r.elapsed_ms = 10;

    char *s = session_preflight_format(&r);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "dirty");
    ASSERT_STR_CONTAINS(s, "15");
    free(s);
    return 0;
}

static int t_context(void) {
    preflight_report_t r;
    memset(&r, 0, sizeof(r));
    r.git_clean = true;
    snprintf(r.git_branch, sizeof(r.git_branch), "feature-x");
    r.feature_file_exists = true;
    r.feature_total = 10;
    r.feature_passing = 7;
    r.progress_file_exists = true;
    r.progress_pending = 3;
    r.progress_done = 5;
    r.episode_count = 2;

    char *ctx = session_preflight_context(&r);
    ASSERT_NOT_NULL(ctx);
    ASSERT_STR_CONTAINS(ctx, "<session_preflight>");
    ASSERT_STR_CONTAINS(ctx, "feature-x");
    ASSERT_STR_CONTAINS(ctx, "7/10");
    ASSERT_STR_CONTAINS(ctx, "3 remaining");
    ASSERT_STR_CONTAINS(ctx, "episodes: 2");
    free(ctx);
    return 0;
}

static int t_context_with_git_log(void) {
    preflight_report_t r;
    memset(&r, 0, sizeof(r));
    r.git_clean = false;
    r.git_dirty_count = 3;
    snprintf(r.git_branch, sizeof(r.git_branch), "dev");
    snprintf(r.git_log, sizeof(r.git_log), "abc1234 fix: bug\ndef5678 feat: new");
    r.feature_file_exists = true;
    r.feature_total = 2;
    r.feature_passing = 1;

    char *ctx = session_preflight_context(&r);
    ASSERT_NOT_NULL(ctx);
    ASSERT_STR_CONTAINS(ctx, "recent_commits:");
    ASSERT_STR_CONTAINS(ctx, "abc1234");
    ASSERT_STR_CONTAINS(ctx, "dirty_files: 3");
    ASSERT_STR_CONTAINS(ctx, "def5678 feat: new\nfeatures: 1/2 passing (1 remaining)");
    free(ctx);
    return 0;
}

static int t_context_no_git_log(void) {
    preflight_report_t r;
    memset(&r, 0, sizeof(r));
    r.git_clean = true;
    r.git_log[0] = '\0';

    char *ctx = session_preflight_context(&r);
    ASSERT_NOT_NULL(ctx);
    ASSERT_TRUE(strstr(ctx, "recent_commits:") == NULL);
    free(ctx);
    return 0;
}

static int t_status_name(void) {
    ASSERT_STR_EQ(preflight_status_name(PREFLIGHT_OK), "ok");
    ASSERT_STR_EQ(preflight_status_name(PREFLIGHT_WARN), "warn");
    ASSERT_STR_EQ(preflight_status_name(PREFLIGHT_DEGRADED), "degraded");
    return 0;
}

static int t_null_format(void) {
    char *s = session_preflight_format(NULL);
    ASSERT_NOT_NULL(s);
    free(s);
    return 0;
}

static int t_null_context(void) {
    char *c = session_preflight_context(NULL);
    ASSERT_NULL(c);
    return 0;
}

static int t_features_display(void) {
    preflight_report_t r;
    memset(&r, 0, sizeof(r));
    r.feature_file_exists = true;
    r.feature_total = 20;
    r.feature_passing = 20;
    r.elapsed_ms = 5;

    char *s = session_preflight_format(&r);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "20/20");
    free(s);
    return 0;
}

int test_session_preflight_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("session_preflight");
    RUN_TEST(t_init);
    RUN_TEST(t_run);
    RUN_TEST(t_run_outside_repo);
    RUN_TEST(t_format);
    RUN_TEST(t_format_dirty);
    RUN_TEST(t_context);
    RUN_TEST(t_context_with_git_log);
    RUN_TEST(t_context_no_git_log);
    RUN_TEST(t_status_name);
    RUN_TEST(t_null_format);
    RUN_TEST(t_null_context);
    RUN_TEST(t_features_display);
    TEST_RETURN_FAILED();
}
