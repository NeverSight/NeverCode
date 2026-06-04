#include "test.h"
#include "nevercode.h"
#include "harness.h"

static int t_init_defaults(void) {
    pipeline_t p;
    pipeline_init(&p, "test-dag", 4);
    ASSERT_STR_EQ(p.name, "test-dag");
    ASSERT_INT_EQ(p.concurrency, 4);
    ASSERT_INT_EQ(p.nnodes, 0);
    ASSERT_INT_EQ(p.status, PIPE_PENDING);
    ASSERT_INT_EQ(p.max_iterations, RCYCLE_DEFAULT_MAX_ITERS);
    pipeline_free(&p);
    return 0;
}

static int t_init_clamps_concurrency(void) {
    pipeline_t p;
    pipeline_init(&p, "cap", PIPE_MAX_CONCURRENT + 10);
    ASSERT_INT_EQ(p.concurrency, PIPE_MAX_CONCURRENT);
    pipeline_free(&p);

    pipeline_init(&p, "zero", 0);
    ASSERT_INT_EQ(p.concurrency, PIPE_MAX_CONCURRENT);
    pipeline_free(&p);

    pipeline_init(&p, "neg", -1);
    ASSERT_INT_EQ(p.concurrency, PIPE_MAX_CONCURRENT);
    pipeline_free(&p);
    return 0;
}

static int t_init_null_name(void) {
    pipeline_t p;
    pipeline_init(&p, NULL, 2);
    ASSERT_INT_EQ(p.name[0], '\0');
    pipeline_free(&p);
    return 0;
}

static int t_add_node_basic(void) {
    pipeline_t p;
    pipeline_init(&p, "nodes", 2);
    ASSERT_INT_EQ(pipeline_add_node(&p, "build", "compile the project"), 0);
    ASSERT_INT_EQ(pipeline_add_node(&p, "test", "run tests"), 1);
    ASSERT_INT_EQ(p.nnodes, 2);
    ASSERT_STR_EQ(p.nodes[0].id, "build");
    ASSERT_STR_EQ(p.nodes[1].id, "test");
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_PENDING);
    pipeline_free(&p);
    return 0;
}

static int t_add_node_rejects_duplicate(void) {
    pipeline_t p;
    pipeline_init(&p, "dup", 2);
    ASSERT_INT_EQ(pipeline_add_node(&p, "a", "first"), 0);
    ASSERT_INT_EQ(pipeline_add_node(&p, "a", "second"), -1);
    ASSERT_INT_EQ(p.nnodes, 1);
    pipeline_free(&p);
    return 0;
}

static int t_add_node_rejects_null(void) {
    pipeline_t p;
    pipeline_init(&p, "null", 2);
    ASSERT_INT_EQ(pipeline_add_node(&p, NULL, "prompt"), -1);
    ASSERT_INT_EQ(pipeline_add_node(&p, "id", NULL), -1);
    ASSERT_INT_EQ(p.nnodes, 0);
    pipeline_free(&p);
    return 0;
}

static int t_add_node_overflow(void) {
    pipeline_t p;
    pipeline_init(&p, "overflow", 2);
    for (int i = 0; i < PIPE_MAX_NODES; i++) {
        char id[NC_SHORT_MAX];
        snprintf(id, sizeof(id), "n%d", i);
        ASSERT_GE(pipeline_add_node(&p, id, "task"), 0);
    }
    ASSERT_INT_EQ(pipeline_add_node(&p, "extra", "too many"), -1);
    ASSERT_INT_EQ(p.nnodes, PIPE_MAX_NODES);
    pipeline_free(&p);
    return 0;
}

static int t_find_node(void) {
    pipeline_t p;
    pipeline_init(&p, "find", 2);
    pipeline_add_node(&p, "alpha", "a");
    pipeline_add_node(&p, "beta", "b");
    ASSERT_INT_EQ(pipeline_find_node(&p, "alpha"), 0);
    ASSERT_INT_EQ(pipeline_find_node(&p, "beta"), 1);
    ASSERT_INT_EQ(pipeline_find_node(&p, "gamma"), -1);
    ASSERT_INT_EQ(pipeline_find_node(&p, NULL), -1);
    pipeline_free(&p);
    return 0;
}

static int t_add_dep_basic(void) {
    pipeline_t p;
    pipeline_init(&p, "dep", 2);
    pipeline_add_node(&p, "a", "first");
    pipeline_add_node(&p, "b", "second");
    ASSERT_INT_EQ(pipeline_add_dep(&p, "b", "a"), 0);
    ASSERT_INT_EQ(p.nodes[1].ndeps, 1);
    ASSERT_STR_EQ(p.nodes[1].deps[0], "a");
    pipeline_free(&p);
    return 0;
}

static int t_add_dep_rejects_unknown(void) {
    pipeline_t p;
    pipeline_init(&p, "dep-bad", 2);
    pipeline_add_node(&p, "a", "first");
    ASSERT_INT_EQ(pipeline_add_dep(&p, "a", "nonexist"), -1);
    ASSERT_INT_EQ(pipeline_add_dep(&p, "nonexist", "a"), -1);
    pipeline_free(&p);
    return 0;
}

static int t_add_dep_dedup(void) {
    pipeline_t p;
    pipeline_init(&p, "dep-dup", 2);
    pipeline_add_node(&p, "a", "first");
    pipeline_add_node(&p, "b", "second");
    ASSERT_INT_EQ(pipeline_add_dep(&p, "b", "a"), 0);
    ASSERT_INT_EQ(pipeline_add_dep(&p, "b", "a"), 0);
    ASSERT_INT_EQ(p.nodes[1].ndeps, 1);
    pipeline_free(&p);
    return 0;
}

static int t_set_retries(void) {
    pipeline_t p;
    pipeline_init(&p, "retry", 2);
    pipeline_add_node(&p, "a", "task");
    ASSERT_INT_EQ(pipeline_set_retries(&p, "a", 3), 0);
    ASSERT_INT_EQ(p.nodes[0].retries, 3);
    ASSERT_INT_EQ(pipeline_set_retries(&p, "a", -1), 0);
    ASSERT_INT_EQ(p.nodes[0].retries, 0);
    ASSERT_INT_EQ(pipeline_set_retries(&p, "missing", 1), -1);
    pipeline_free(&p);
    return 0;
}

static int t_validate_empty_fails(void) {
    pipeline_t p;
    pipeline_init(&p, "empty", 2);
    char err[NC_LABEL_MAX];
    ASSERT_FALSE(pipeline_validate(&p, err, sizeof(err)));
    ASSERT_STR_CONTAINS(err, "no nodes");
    pipeline_free(&p);
    return 0;
}

static int t_validate_good_dag(void) {
    pipeline_t p;
    pipeline_init(&p, "dag", 2);
    pipeline_add_node(&p, "a", "first");
    pipeline_add_node(&p, "b", "second");
    pipeline_add_dep(&p, "b", "a");
    ASSERT_TRUE(pipeline_validate(&p, NULL, 0));
    pipeline_free(&p);
    return 0;
}

static int t_validate_cycle_detected(void) {
    pipeline_t p;
    pipeline_init(&p, "cycle", 2);
    pipeline_add_node(&p, "a", "first");
    pipeline_add_node(&p, "b", "second");
    pipeline_add_dep(&p, "b", "a");
    pipeline_add_dep(&p, "a", "b");
    char err[NC_LABEL_MAX];
    ASSERT_FALSE(pipeline_validate(&p, err, sizeof(err)));
    ASSERT_STR_CONTAINS(err, "cycle");
    pipeline_free(&p);
    return 0;
}

static int t_validate_unknown_dep(void) {
    pipeline_t p;
    pipeline_init(&p, "bad-dep", 2);
    pipeline_add_node(&p, "a", "task");
    p.nodes[0].ndeps = 1;
    nc_strlcpy(p.nodes[0].deps[0], "ghost", sizeof(p.nodes[0].deps[0]));
    char err[NC_LABEL_MAX];
    ASSERT_FALSE(pipeline_validate(&p, err, sizeof(err)));
    ASSERT_STR_CONTAINS(err, "unknown dep");
    pipeline_free(&p);
    return 0;
}

static int t_ready_nodes_no_deps(void) {
    pipeline_t p;
    pipeline_init(&p, "ready", 2);
    pipeline_add_node(&p, "a", "first");
    pipeline_add_node(&p, "b", "second");
    int ready[PIPE_MAX_NODES];
    int n = pipeline_ready_nodes(&p, ready, PIPE_MAX_NODES);
    ASSERT_INT_EQ(n, 2);
    return 0;
}

static int t_ready_nodes_with_deps(void) {
    pipeline_t p;
    pipeline_init(&p, "ready-dep", 2);
    pipeline_add_node(&p, "a", "first");
    pipeline_add_node(&p, "b", "second");
    pipeline_add_dep(&p, "b", "a");
    int ready[PIPE_MAX_NODES];
    int n = pipeline_ready_nodes(&p, ready, PIPE_MAX_NODES);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(ready[0], 0);
    pipeline_free(&p);
    return 0;
}

static int t_mark_running(void) {
    pipeline_t p;
    pipeline_init(&p, "run", 2);
    pipeline_add_node(&p, "a", "task");
    ASSERT_INT_EQ(pipeline_mark_running(&p, 0), 0);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_RUNNING);
    ASSERT_INT_EQ(p.nodes[0].attempt, 1);
    ASSERT_INT_EQ(p.status, PIPE_RUNNING);
    ASSERT_TRUE(p.started_ms > 0);
    ASSERT_INT_EQ(pipeline_mark_running(&p, -1), -1);
    ASSERT_INT_EQ(pipeline_mark_running(&p, PIPE_MAX_NODES), -1);
    pipeline_free(&p);
    return 0;
}

static int t_mark_done(void) {
    pipeline_t p;
    pipeline_init(&p, "done", 2);
    pipeline_add_node(&p, "a", "task");
    pipeline_mark_running(&p, 0);
    ASSERT_INT_EQ(pipeline_mark_done(&p, 0, "result data", 0), 0);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_DONE);
    ASSERT_INT_EQ(p.nodes[0].exit_code, 0);
    ASSERT_STR_EQ(p.nodes[0].output, "result data");
    ASSERT_TRUE(p.nodes[0].finished_ms > 0);
    pipeline_free(&p);
    return 0;
}

static int t_mark_done_null_output(void) {
    pipeline_t p;
    pipeline_init(&p, "done-null", 2);
    pipeline_add_node(&p, "a", "task");
    pipeline_mark_running(&p, 0);
    ASSERT_INT_EQ(pipeline_mark_done(&p, 0, NULL, 0), 0);
    ASSERT_NULL(p.nodes[0].output);
    pipeline_free(&p);
    return 0;
}

static int t_mark_failed(void) {
    pipeline_t p;
    pipeline_init(&p, "fail", 2);
    pipeline_add_node(&p, "a", "task");
    pipeline_mark_running(&p, 0);
    ASSERT_INT_EQ(pipeline_mark_failed(&p, 0, "error msg", 1), 0);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_FAILED);
    ASSERT_INT_EQ(p.nodes[0].exit_code, 1);
    ASSERT_STR_EQ(p.nodes[0].output, "error msg");
    pipeline_free(&p);
    return 0;
}

static int t_mark_cancelled(void) {
    pipeline_t p;
    pipeline_init(&p, "cancel", 2);
    pipeline_add_node(&p, "a", "task");
    ASSERT_INT_EQ(pipeline_mark_cancelled(&p, 0), 0);
    ASSERT_INT_EQ(p.nodes[0].status, PIPE_CANCELLED);
    ASSERT_TRUE(p.nodes[0].finished_ms > 0);
    ASSERT_INT_EQ(pipeline_mark_cancelled(&p, -1), -1);
    pipeline_free(&p);
    return 0;
}

static int t_skip_downstream(void) {
    pipeline_t p;
    pipeline_init(&p, "skip", 2);
    pipeline_add_node(&p, "a", "root");
    pipeline_add_node(&p, "b", "child");
    pipeline_add_node(&p, "c", "grandchild");
    pipeline_add_dep(&p, "b", "a");
    pipeline_add_dep(&p, "c", "b");
    pipeline_mark_running(&p, 0);
    pipeline_mark_failed(&p, 0, "crash", 1);
    int skipped = pipeline_skip_downstream(&p, 0);
    ASSERT_INT_EQ(skipped, 2);
    ASSERT_INT_EQ(p.nodes[1].status, PIPE_SKIPPED);
    ASSERT_INT_EQ(p.nodes[2].status, PIPE_SKIPPED);
    pipeline_free(&p);
    return 0;
}

static int t_skip_no_effect_on_independent(void) {
    pipeline_t p;
    pipeline_init(&p, "skip-ind", 2);
    pipeline_add_node(&p, "a", "root");
    pipeline_add_node(&p, "b", "independent");
    pipeline_mark_running(&p, 0);
    pipeline_mark_failed(&p, 0, "fail", 1);
    int skipped = pipeline_skip_downstream(&p, 0);
    ASSERT_INT_EQ(skipped, 0);
    ASSERT_INT_EQ(p.nodes[1].status, PIPE_PENDING);
    pipeline_free(&p);
    return 0;
}

static int t_all_terminal(void) {
    pipeline_t p;
    pipeline_init(&p, "term", 2);
    pipeline_add_node(&p, "a", "first");
    pipeline_add_node(&p, "b", "second");
    ASSERT_FALSE(pipeline_all_terminal(&p));
    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "ok", 0);
    ASSERT_FALSE(pipeline_all_terminal(&p));
    pipeline_mark_running(&p, 1);
    pipeline_mark_done(&p, 1, "ok", 0);
    ASSERT_TRUE(pipeline_all_terminal(&p));
    pipeline_free(&p);
    return 0;
}

static int t_status_name(void) {
    ASSERT_STR_EQ(pipe_status_name(PIPE_PENDING), "pending");
    ASSERT_STR_EQ(pipe_status_name(PIPE_RUNNING), "running");
    ASSERT_STR_EQ(pipe_status_name(PIPE_DONE), "done");
    ASSERT_STR_EQ(pipe_status_name(PIPE_FAILED), "failed");
    ASSERT_STR_EQ(pipe_status_name(PIPE_SKIPPED), "skipped");
    ASSERT_STR_EQ(pipe_status_name(PIPE_CANCELLED), "cancelled");
    return 0;
}

static int t_format_status(void) {
    pipeline_t p;
    pipeline_init(&p, "fmt", 2);
    pipeline_add_node(&p, "build", "compile");
    pipeline_add_node(&p, "test", "verify");
    pipeline_add_dep(&p, "test", "build");
    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "ok", 0);
    char *s = pipeline_format_status(&p);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "fmt");
    ASSERT_STR_CONTAINS(s, "build");
    ASSERT_STR_CONTAINS(s, "test");
    ASSERT_STR_CONTAINS(s, "<- build");
    free(s);
    pipeline_free(&p);
    return 0;
}

static int t_three_layer_dag(void) {
    pipeline_t p;
    pipeline_init(&p, "3layer", 4);
    pipeline_add_node(&p, "fetch", "download deps");
    pipeline_add_node(&p, "build", "compile");
    pipeline_add_node(&p, "lint", "static analysis");
    pipeline_add_node(&p, "test", "run tests");
    pipeline_add_node(&p, "deploy", "ship it");
    pipeline_add_dep(&p, "build", "fetch");
    pipeline_add_dep(&p, "lint", "fetch");
    pipeline_add_dep(&p, "test", "build");
    pipeline_add_dep(&p, "deploy", "test");
    pipeline_add_dep(&p, "deploy", "lint");
    ASSERT_TRUE(pipeline_validate(&p, NULL, 0));

    int ready[PIPE_MAX_NODES];
    ASSERT_INT_EQ(pipeline_ready_nodes(&p, ready, PIPE_MAX_NODES), 1);
    ASSERT_INT_EQ(ready[0], 0);

    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "deps ok", 0);
    int n = pipeline_ready_nodes(&p, ready, PIPE_MAX_NODES);
    ASSERT_INT_EQ(n, 2);

    pipeline_mark_running(&p, 1);
    pipeline_mark_running(&p, 2);
    pipeline_mark_done(&p, 1, "built", 0);
    pipeline_mark_done(&p, 2, "clean", 0);
    n = pipeline_ready_nodes(&p, ready, PIPE_MAX_NODES);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(ready[0], 3);

    pipeline_mark_running(&p, 3);
    pipeline_mark_done(&p, 3, "pass", 0);
    n = pipeline_ready_nodes(&p, ready, PIPE_MAX_NODES);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(ready[0], 4);

    pipeline_mark_running(&p, 4);
    pipeline_mark_done(&p, 4, "deployed", 0);
    ASSERT_TRUE(pipeline_all_terminal(&p));
    pipeline_free(&p);
    return 0;
}

static int t_free_clears_output(void) {
    pipeline_t p;
    pipeline_init(&p, "free", 2);
    pipeline_add_node(&p, "a", "task");
    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "some output data", 0);
    pipeline_free(&p);
    ASSERT_INT_EQ(p.nnodes, 0);
    return 0;
}

int test_pipeline_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("pipeline");
    RUN_TEST(t_init_defaults);
    RUN_TEST(t_init_clamps_concurrency);
    RUN_TEST(t_init_null_name);
    RUN_TEST(t_add_node_basic);
    RUN_TEST(t_add_node_rejects_duplicate);
    RUN_TEST(t_add_node_rejects_null);
    RUN_TEST(t_add_node_overflow);
    RUN_TEST(t_find_node);
    RUN_TEST(t_add_dep_basic);
    RUN_TEST(t_add_dep_rejects_unknown);
    RUN_TEST(t_add_dep_dedup);
    RUN_TEST(t_set_retries);
    RUN_TEST(t_validate_empty_fails);
    RUN_TEST(t_validate_good_dag);
    RUN_TEST(t_validate_cycle_detected);
    RUN_TEST(t_validate_unknown_dep);
    RUN_TEST(t_ready_nodes_no_deps);
    RUN_TEST(t_ready_nodes_with_deps);
    RUN_TEST(t_mark_running);
    RUN_TEST(t_mark_done);
    RUN_TEST(t_mark_done_null_output);
    RUN_TEST(t_mark_failed);
    RUN_TEST(t_mark_cancelled);
    RUN_TEST(t_skip_downstream);
    RUN_TEST(t_skip_no_effect_on_independent);
    RUN_TEST(t_all_terminal);
    RUN_TEST(t_status_name);
    RUN_TEST(t_format_status);
    RUN_TEST(t_three_layer_dag);
    RUN_TEST(t_free_clears_output);
    TEST_RETURN_FAILED();
}
