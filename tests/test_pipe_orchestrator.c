#include "test.h"
#include "nevercode.h"
#include "harness.h"

static int t_init_free(void) {
    static pipeline_t p;
    pipeline_init(&p, "orch-test", PIPE_MAX_CONCURRENT);
    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    ASSERT_INT_EQ(o.state, ORCH_IDLE);
    ASSERT(o.pipeline == &p);
    ASSERT_INT_EQ(o.ncriteria, 0);
    ASSERT_INT_EQ(o.nprobes, 0);
    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_begin_validates(void) {
    static pipeline_t p;
    pipeline_init(&p, "v-test", PIPE_MAX_CONCURRENT);
    pipeline_add_node(&p, "a", "do A");
    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    ASSERT_INT_EQ(pipe_orch_begin(&o), 0);
    ASSERT_INT_EQ(o.state, ORCH_RUNNING);
    ASSERT_TRUE(o.run_scope.token[0] != '\0');
    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_begin_empty_fails(void) {
    static pipeline_t p;
    pipeline_init(&p, "empty", PIPE_MAX_CONCURRENT);
    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    ASSERT_INT_EQ(pipe_orch_begin(&o), -1);
    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_add_criteria(void) {
    static pipeline_t p;
    pipeline_init(&p, "cr-test", PIPE_MAX_CONCURRENT);
    pipeline_add_node(&p, "build", "compile");
    static pipe_orch_t o;
    pipe_orch_init(&o, &p);

    sc_ruleset_t rs;
    sc_ruleset_init(&rs);
    sc_add_exit_zero(&rs);

    ASSERT_INT_EQ(pipe_orch_add_criteria(&o, "build", &rs), 0);
    ASSERT_INT_EQ(o.ncriteria, 1);
    ASSERT_INT_EQ(pipe_orch_add_criteria(&o, "nonexistent", &rs), -1);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_add_probe(void) {
    static pipeline_t p;
    pipeline_init(&p, "pr-test", PIPE_MAX_CONCURRENT);
    pipeline_add_node(&p, "w", "work");
    static pipe_orch_t o;
    pipe_orch_init(&o, &p);

    ASSERT_INT_EQ(pipe_orch_add_probe(&o, "workers", 30), 0);
    ASSERT_INT_EQ(o.nprobes, 1);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_step_idle_noop(void) {
    static pipeline_t p;
    pipeline_init(&p, "idle", PIPE_MAX_CONCURRENT);
    pipeline_add_node(&p, "a", "test");
    static pipe_orch_t o;
    pipe_orch_init(&o, &p);

    int ready[4];
    int n = pipe_orch_step(&o, ready, 4);
    ASSERT_INT_EQ(n, 0);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_step_schedules(void) {
    static pipeline_t p;
    pipeline_init(&p, "sched", PIPE_MAX_CONCURRENT);
    pipeline_add_node(&p, "task1", "do task 1");
    pipeline_add_node(&p, "task2", "do task 2");
    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    int ready[8];
    int n = pipe_orch_step(&o, ready, 8);
    ASSERT_GT(n, 0);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_format_status(void) {
    static pipeline_t p;
    pipeline_init(&p, "fmt", PIPE_MAX_CONCURRENT);
    pipeline_add_node(&p, "x", "work");
    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    char *s = pipe_orch_format_status(&o);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "fmt");
    free(s);

    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

int test_pipe_orchestrator_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("pipe_orchestrator");
    RUN_TEST(t_init_free);
    RUN_TEST(t_begin_validates);
    RUN_TEST(t_begin_empty_fails);
    RUN_TEST(t_add_criteria);
    RUN_TEST(t_add_probe);
    RUN_TEST(t_step_idle_noop);
    RUN_TEST(t_step_schedules);
    RUN_TEST(t_format_status);
    TEST_RETURN_FAILED();
}
