#include "test.h"
#include "nevercode.h"
#include "harness.h"

static int t_init_empty(void) {
    scope_aggregate_t sa;
    scope_agg_init(&sa, "grp");
    ASSERT_STR_EQ(sa.group_id, "grp");
    ASSERT_INT_EQ(sa.nmembers, 0);
    scope_summary_t s = scope_agg_summarize(&sa);
    ASSERT_INT_EQ(s.total, 0);
    scope_agg_free(&sa);
    return 0;
}

static int t_manual_members(void) {
    scope_aggregate_t sa;
    scope_agg_init(&sa, "test");

    sa.members[0].status = PIPE_DONE;
    sa.members[0].output = nc_xstrdup("result A");
    nc_strlcpy(sa.members[0].node_id, "n1", sizeof(sa.members[0].node_id));

    sa.members[1].status = PIPE_FAILED;
    sa.members[1].output = NULL;
    nc_strlcpy(sa.members[1].node_id, "n2", sizeof(sa.members[1].node_id));

    sa.nmembers = 2;
    sa.summary = scope_agg_summarize(&sa);

    ASSERT_INT_EQ(sa.summary.total, 2);
    ASSERT_INT_EQ(sa.summary.completed, 1);
    ASSERT_INT_EQ(sa.summary.failed, 1);
    ASSERT_INT_EQ(sa.summary.with_output, 1);
    ASSERT_INT_EQ(sa.summary.without_output, 1);

    ASSERT_INT_EQ(scope_agg_count_with_output(&sa), 1);
    ASSERT_INT_EQ(scope_agg_count_by_status(&sa, PIPE_DONE), 1);
    ASSERT_INT_EQ(scope_agg_count_by_status(&sa, PIPE_FAILED), 1);
    ASSERT_INT_EQ(scope_agg_count_by_status(&sa, PIPE_PENDING), 0);

    scope_agg_free(&sa);
    return 0;
}

static int t_joined_outputs(void) {
    scope_aggregate_t sa;
    scope_agg_init(&sa, "j");

    sa.members[0].status = PIPE_DONE;
    sa.members[0].output = nc_xstrdup("alpha");
    nc_strlcpy(sa.members[0].node_id, "a", sizeof(sa.members[0].node_id));

    sa.members[1].status = PIPE_DONE;
    sa.members[1].output = nc_xstrdup("beta");
    nc_strlcpy(sa.members[1].node_id, "b", sizeof(sa.members[1].node_id));

    sa.nmembers = 2;

    char *joined = scope_agg_joined_outputs(&sa);
    ASSERT_NOT_NULL(joined);
    ASSERT_STR_CONTAINS(joined, "alpha");
    ASSERT_STR_CONTAINS(joined, "beta");
    free(joined);

    scope_agg_free(&sa);
    return 0;
}

static int t_filtered_outputs(void) {
    scope_aggregate_t sa;
    scope_agg_init(&sa, "f");

    sa.members[0].status = PIPE_DONE;
    sa.members[0].output = nc_xstrdup("good");
    nc_strlcpy(sa.members[0].node_id, "ok", sizeof(sa.members[0].node_id));

    sa.members[1].status = PIPE_FAILED;
    sa.members[1].output = nc_xstrdup("bad");
    nc_strlcpy(sa.members[1].node_id, "fail", sizeof(sa.members[1].node_id));

    sa.nmembers = 2;

    char *done_only = scope_agg_filtered_outputs(&sa, PIPE_DONE);
    ASSERT_NOT_NULL(done_only);
    ASSERT_STR_CONTAINS(done_only, "good");
    ASSERT_FALSE(strstr(done_only, "bad") != NULL);
    free(done_only);

    scope_agg_free(&sa);
    return 0;
}

static int t_format_output(void) {
    scope_aggregate_t sa;
    scope_agg_init(&sa, "fmt");
    sa.nmembers = 0;
    sa.summary = scope_agg_summarize(&sa);

    char *fmt = scope_agg_format(&sa);
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "fmt");
    ASSERT_STR_CONTAINS(fmt, "0 members");
    free(fmt);

    scope_agg_free(&sa);
    return 0;
}

static int t_subset(void) {
    scope_aggregate_t sa;
    scope_agg_init(&sa, "sub");

    sa.members[0].status = PIPE_DONE;
    sa.members[0].output = nc_xstrdup("x");
    nc_strlcpy(sa.members[0].node_id, "d1", sizeof(sa.members[0].node_id));

    sa.members[1].status = PIPE_FAILED;
    sa.members[1].output = nc_xstrdup("y");
    nc_strlcpy(sa.members[1].node_id, "f1", sizeof(sa.members[1].node_id));

    sa.members[2].status = PIPE_DONE;
    sa.members[2].output = nc_xstrdup("z");
    nc_strlcpy(sa.members[2].node_id, "d2", sizeof(sa.members[2].node_id));

    sa.nmembers = 3;
    sa.summary = scope_agg_summarize(&sa);

    scope_aggregate_t dst;
    scope_agg_init(&dst, "");
    int n = scope_agg_subset(&dst, &sa, PIPE_DONE);
    ASSERT_INT_EQ(n, 2);
    ASSERT_INT_EQ(dst.summary.completed, 2);

    scope_agg_free(&dst);
    scope_agg_free(&sa);
    return 0;
}

int test_scope_aggregate_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("scope_aggregate");
    RUN_TEST(t_init_empty);
    RUN_TEST(t_manual_members);
    RUN_TEST(t_joined_outputs);
    RUN_TEST(t_filtered_outputs);
    RUN_TEST(t_format_output);
    RUN_TEST(t_subset);
    TEST_RETURN_FAILED();
}
