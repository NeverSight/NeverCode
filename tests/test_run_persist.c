#include "test.h"
#include "nevercode.h"
#include "harness.h"

static int t_snapshot_json_basic(void) {
    static pipeline_t p;
    pipeline_init(&p, "persist-test", PIPE_MAX_CONCURRENT);
    pipeline_add_node(&p, "step1", "do work");
    pipeline_add_node(&p, "step2", "verify");
    pipeline_add_dep(&p, "step2", "step1");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    pipe_orch_begin(&o);

    stall_detector_init();
    context_fatigue_init(128000);
    failure_router_init();
    tool_verify_init();
    output_eval_init();
    auto_test_init(NULL);
    checkpoint_discipline_init(NULL, 10);
    doom_loop_init();

    char *json = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(json);
    ASSERT_STR_CONTAINS(json, "\"magic\":");
    ASSERT_STR_CONTAINS(json, RUN_PERSIST_MAGIC);
    ASSERT_STR_CONTAINS(json, "persist-test");
    ASSERT_STR_CONTAINS(json, "step1");
    ASSERT_STR_CONTAINS(json, "step2");
    ASSERT_STR_CONTAINS(json, "\"nodes\":");
    ASSERT_STR_CONTAINS(json, "\"watchdog\":");
    ASSERT_STR_CONTAINS(json, "\"stall_detector\":");
    free(json);

    pipe_orch_free(&o);
    pipeline_free(&p);
    stall_detector_free();
    context_fatigue_free();
    failure_router_free();
    tool_verify_free();
    output_eval_free();
    auto_test_free();
    checkpoint_discipline_free();
    return 0;
}

static int t_snapshot_null(void) {
    ASSERT_NULL(run_persist_snapshot_json(NULL));
    static pipe_orch_t o;
    memset(&o, 0, sizeof(o));
    ASSERT_NULL(run_persist_snapshot_json(&o));
    return 0;
}

static int t_roundtrip(void) {
    static pipeline_t p;
    pipeline_init(&p, "roundtrip", PIPE_MAX_CONCURRENT);
    pipeline_add_node(&p, "a", "task A");
    pipeline_add_node(&p, "b", "task B");
    pipeline_add_dep(&p, "b", "a");

    static pipe_orch_t o;
    pipe_orch_init(&o, &p);

    stall_detector_init();
    context_fatigue_init(128000);
    failure_router_init();
    tool_verify_init();
    output_eval_init();
    auto_test_init(NULL);
    checkpoint_discipline_init(NULL, 10);
    doom_loop_init();

    pipe_orch_begin(&o);
    pipeline_mark_running(&p, 0);
    pipeline_mark_done(&p, 0, "output-A", 0);

    char *json = run_persist_snapshot_json(&o);
    ASSERT_NOT_NULL(json);

    static pipeline_t p2;
    pipeline_init(&p2, "roundtrip", PIPE_MAX_CONCURRENT);
    pipeline_add_node(&p2, "a", "task A");
    pipeline_add_node(&p2, "b", "task B");
    pipeline_add_dep(&p2, "b", "a");

    static pipe_orch_t o2;
    pipe_orch_init(&o2, &p2);
    int rc = run_persist_restore_json(&o2, &p2, json);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(o2.state, ORCH_RUNNING);

    int idx_a = pipeline_find_node(&p2, "a");
    ASSERT_GE(idx_a, 0);
    ASSERT_INT_EQ(p2.nodes[idx_a].status, PIPE_DONE);
    ASSERT_NOT_NULL(p2.nodes[idx_a].output);
    ASSERT_STR_EQ(p2.nodes[idx_a].output, "output-A");

    free(json);
    pipe_orch_free(&o);
    pipe_orch_free(&o2);
    pipeline_free(&p);
    pipeline_free(&p2);
    stall_detector_free();
    context_fatigue_free();
    failure_router_free();
    tool_verify_free();
    output_eval_free();
    auto_test_free();
    checkpoint_discipline_free();
    return 0;
}

static int t_restore_bad_magic(void) {
    static pipeline_t p;
    pipeline_init(&p, "bad", PIPE_MAX_CONCURRENT);
    static pipe_orch_t o;
    pipe_orch_init(&o, &p);
    ASSERT_INT_EQ(run_persist_restore_json(&o, &p, "{\"magic\":\"WRONG\"}"), -1);
    ASSERT_INT_EQ(run_persist_restore_json(&o, &p, "not json"), -1);
    ASSERT_INT_EQ(run_persist_restore_json(NULL, NULL, NULL), -1);
    pipe_orch_free(&o);
    pipeline_free(&p);
    return 0;
}

static int t_save_null(void) {
    ASSERT_INT_EQ(run_persist_save(NULL, NULL), -1);
    return 0;
}

int test_run_persist_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("run_persist");
    RUN_TEST(t_snapshot_json_basic);
    RUN_TEST(t_snapshot_null);
    RUN_TEST(t_roundtrip);
    RUN_TEST(t_restore_bad_magic);
    RUN_TEST(t_save_null);
    TEST_RETURN_FAILED();
}
