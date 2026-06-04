#include "test.h"
#include "nevercode.h"
#include "harness.h"

static int t_init(void) {
    pflt_report_t r;
    pflt_report_init(&r);
    ASSERT_INT_EQ(r.nsteps, 0);
    ASSERT_INT_EQ(r.nwarnings, 0);
    ASSERT_FALSE(r.has_cycles);
    return 0;
}

static int t_add_warning(void) {
    pflt_report_t r;
    pflt_report_init(&r);
    ASSERT_INT_EQ(pflt_add_warning(&r, "test warning"), 0);
    ASSERT_INT_EQ(r.nwarnings, 1);
    ASSERT_INT_EQ(pflt_add_warning(&r, NULL), -1);
    return 0;
}

static int t_warning_overflow(void) {
    pflt_report_t r;
    pflt_report_init(&r);
    for (int i = 0; i < PFLT_MAX_WARNINGS; i++)
        ASSERT_INT_EQ(pflt_add_warning(&r, "w"), 0);
    ASSERT_INT_EQ(pflt_add_warning(&r, "extra"), -1);
    ASSERT_INT_EQ(r.nwarnings, PFLT_MAX_WARNINGS);
    return 0;
}

static int t_build_simple(void) {
    pipeline_t p;
    pipeline_init(&p, "build-test", PIPE_MAX_CONCURRENT);
    pipeline_add_node(&p, "step1", "compile code");
    pipeline_add_node(&p, "step2", "run tests");
    pipeline_add_dep(&p, "step2", "step1");

    pflt_report_t r;
    ASSERT_INT_EQ(pflt_build_from_pipeline(&r, &p), 0);
    ASSERT_INT_EQ(r.nsteps, 2);
    ASSERT_INT_EQ(r.total_agents, 2);
    ASSERT_FALSE(r.has_cycles);
    ASSERT_STR_EQ(r.pipeline_name, "build-test");

    pipeline_free(&p);
    return 0;
}

static int t_build_null(void) {
    pflt_report_t r;
    ASSERT_INT_EQ(pflt_build_from_pipeline(&r, NULL), -1);
    return 0;
}

static int t_orphan_warning(void) {
    pipeline_t p;
    pipeline_init(&p, "orphan", PIPE_MAX_CONCURRENT);
    pipeline_add_node(&p, "connected", "work");
    pipeline_add_node(&p, "isolated", "alone");
    pipeline_add_node(&p, "depends", "after connected");
    pipeline_add_dep(&p, "depends", "connected");

    pflt_report_t r;
    pflt_build_from_pipeline(&r, &p);
    ASSERT_TRUE(pflt_has_issues(&r));
    ASSERT_GT(r.nwarnings, 0);

    pipeline_free(&p);
    return 0;
}

static int t_format_report(void) {
    pipeline_t p;
    pipeline_init(&p, "fmt-test", PIPE_MAX_CONCURRENT);
    pipeline_add_node(&p, "a", "do something");

    pflt_report_t r;
    pflt_build_from_pipeline(&r, &p);
    char *report = pflt_format_report(&r);
    ASSERT_NOT_NULL(report);
    ASSERT_STR_CONTAINS(report, "fmt-test");
    ASSERT_STR_CONTAINS(report, "Execution order");
    free(report);

    pipeline_free(&p);
    return 0;
}

static int t_format_summary(void) {
    pipeline_t p;
    pipeline_init(&p, "sum", PIPE_MAX_CONCURRENT);
    pipeline_add_node(&p, "x", "y");

    pflt_report_t r;
    pflt_build_from_pipeline(&r, &p);
    char *s = pflt_format_summary(&r);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "sum");
    ASSERT_STR_CONTAINS(s, "1 nodes");
    free(s);

    pipeline_free(&p);
    return 0;
}

static int t_has_issues(void) {
    pflt_report_t r;
    pflt_report_init(&r);
    ASSERT_FALSE(pflt_has_issues(&r));
    pflt_add_warning(&r, "problem");
    ASSERT_TRUE(pflt_has_issues(&r));

    pflt_report_t r2;
    pflt_report_init(&r2);
    r2.has_cycles = true;
    ASSERT_TRUE(pflt_has_issues(&r2));
    return 0;
}

int test_preflight_report_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("preflight_report");
    RUN_TEST(t_init);
    RUN_TEST(t_add_warning);
    RUN_TEST(t_warning_overflow);
    RUN_TEST(t_build_simple);
    RUN_TEST(t_build_null);
    RUN_TEST(t_orphan_warning);
    RUN_TEST(t_format_report);
    RUN_TEST(t_format_summary);
    RUN_TEST(t_has_issues);
    TEST_RETURN_FAILED();
}
