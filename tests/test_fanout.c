#include "test.h"
#include "nevercode.h"
#include "harness.h"

static int t_init_defaults(void) {
    fanout_spec_t f;
    fanout_init(&f, "grp", "do work on {{item}}");
    ASSERT_STR_EQ(f.group_id, "grp");
    ASSERT_INT_EQ(f.mode, FANOUT_COUNT);
    ASSERT_INT_EQ(f.nexpanded, 0);
    fanout_free(&f);
    return 0;
}

static int t_set_count(void) {
    fanout_spec_t f;
    fanout_init(&f, "g", "tpl");
    ASSERT_INT_EQ(fanout_set_count(&f, 5), 0);
    ASSERT_INT_EQ(f.count, 5);
    ASSERT_INT_EQ(fanout_set_count(&f, 0), -1);
    ASSERT_INT_EQ(fanout_set_count(&f, FANOUT_MAX_COPIES + 1), -1);
    fanout_free(&f);
    return 0;
}

static int t_add_values(void) {
    fanout_spec_t f;
    fanout_init(&f, "v", "process");
    ASSERT_INT_EQ(fanout_add_value(&f, "alpha"), 0);
    ASSERT_INT_EQ(fanout_add_value(&f, "beta"), 0);
    ASSERT_INT_EQ(f.nvalues, 2);
    ASSERT_INT_EQ(f.mode, FANOUT_VALUES);
    ASSERT_INT_EQ(fanout_add_value(&f, NULL), -1);
    fanout_free(&f);
    return 0;
}

static int t_expand_count(void) {
    fanout_spec_t f;
    fanout_init(&f, "workers", "run shard");
    fanout_set_count(&f, 3);
    int n = fanout_expand(&f, NULL);
    ASSERT_INT_EQ(n, 3);
    ASSERT_INT_EQ(fanout_expanded_count(&f), 3);
    ASSERT_NOT_NULL(fanout_expanded_id(&f, 0));
    ASSERT_NOT_NULL(fanout_expanded_id(&f, 2));
    ASSERT_NULL(fanout_expanded_id(&f, 3));
    ASSERT_NULL(fanout_expanded_id(&f, -1));
    ASSERT_STR_CONTAINS(fanout_expanded_id(&f, 0), "workers_");
    fanout_free(&f);
    return 0;
}

static int t_expand_values_into_pipeline(void) {
    pipeline_t p;
    pipeline_init(&p, "test-pipe", PIPE_MAX_CONCURRENT);
    fanout_spec_t f;
    fanout_init(&f, "items", "process {{item}}");
    fanout_add_value(&f, "file1.c");
    fanout_add_value(&f, "file2.c");
    int n = fanout_expand(&f, &p);
    ASSERT_INT_EQ(n, 2);
    ASSERT_INT_EQ(p.nnodes, 2);
    fanout_free(&f);
    pipeline_free(&p);
    return 0;
}

static int t_merge_batch(void) {
    merge_spec_t m;
    merge_init(&m, "reduce", "workers");
    ASSERT_INT_EQ(m.mode, MERGE_BATCH);
    ASSERT_INT_EQ(m.batch_size, FANOUT_BATCH_MAX);
    ASSERT_INT_EQ(merge_set_batch(&m, 2), 0);
    ASSERT_INT_EQ(m.batch_size, 2);
    ASSERT_INT_EQ(merge_set_batch(&m, 0), -1);
    return 0;
}

static int t_merge_group(void) {
    merge_spec_t m;
    merge_init(&m, "agg", "src");
    ASSERT_INT_EQ(merge_set_group(&m, "category"), 0);
    ASSERT_INT_EQ(m.mode, MERGE_GROUP);
    ASSERT_INT_EQ(merge_set_group(&m, NULL), -1);
    return 0;
}

static int t_merge_expand(void) {
    pipeline_t p;
    pipeline_init(&p, "mr-pipe", PIPE_MAX_CONCURRENT);
    fanout_spec_t f;
    fanout_init(&f, "map", "analyze");
    fanout_set_count(&f, 4);
    fanout_expand(&f, &p);

    merge_spec_t m;
    merge_init(&m, "reduce", "map");
    merge_set_batch(&m, 2);
    int nb = merge_expand(&m, &f, &p, "summarize batch");
    ASSERT_INT_EQ(nb, 2);
    ASSERT_GT(p.nnodes, 4);

    fanout_free(&f);
    pipeline_free(&p);
    return 0;
}

static int t_merge_null_rejected(void) {
    merge_spec_t m;
    merge_init(&m, "r", "s");
    ASSERT_INT_EQ(merge_expand(&m, NULL, NULL, "x"), -1);
    return 0;
}

int test_fanout_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("fanout");
    RUN_TEST(t_init_defaults);
    RUN_TEST(t_set_count);
    RUN_TEST(t_add_values);
    RUN_TEST(t_expand_count);
    RUN_TEST(t_expand_values_into_pipeline);
    RUN_TEST(t_merge_batch);
    RUN_TEST(t_merge_group);
    RUN_TEST(t_merge_expand);
    RUN_TEST(t_merge_null_rejected);
    TEST_RETURN_FAILED();
}
