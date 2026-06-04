#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

static int test_init(void) {
    rollout_meta_t m;
    rollout_meta_init(&m);
    ASSERT_INT_EQ(m.turn_count, 0);
    ASSERT(m.started_at > 0);
    return 0;
}
static int test_record(void) {
    rollout_meta_t m;
    rollout_meta_init(&m);
    rollout_meta_set_session(&m, "s1", "gpt-5.4", "/tmp");
    rollout_meta_record_turn(&m, 3, 1000, 500, 0.05);
    rollout_meta_record_turn(&m, 1, 500, 200, 0.02);
    ASSERT_INT_EQ(m.turn_count, 2);
    ASSERT_INT_EQ(m.tool_call_count, 4);
    ASSERT_INT_EQ(m.input_tokens, 1500);
    return 0;
}
static int test_compact(void) {
    rollout_meta_t m;
    rollout_meta_init(&m);
    rollout_meta_record_compact(&m);
    rollout_meta_record_compact(&m);
    ASSERT_INT_EQ(m.compact_count, 2);
    return 0;
}
static int test_serialize(void) {
    rollout_meta_t m;
    rollout_meta_init(&m);
    rollout_meta_set_session(&m, "s1", "gpt-5.4", "/tmp");
    rollout_meta_record_turn(&m, 2, 100, 50, 0.01);
    rollout_meta_finalize(&m, "test summary");
    char *json = rollout_meta_serialize(&m);
    ASSERT_NOT_NULL(json);
    rollout_meta_t m2;
    ASSERT_INT_EQ(rollout_meta_deserialize(&m2, json), 0);
    ASSERT_STR_EQ(m2.session_id, "s1");
    ASSERT_STR_EQ(m2.model, "gpt-5.4");
    ASSERT_INT_EQ(m2.turn_count, 1);
    free(json);
    return 0;
}
static int test_format(void) {
    rollout_meta_t m;
    rollout_meta_init(&m);
    rollout_meta_set_session(&m, "s1", "model1", "/ws");
    char *s = rollout_meta_format(&m);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Session: s1");
    free(s);
    return 0;
}
static int test_duration(void) {
    rollout_meta_t m;
    rollout_meta_init(&m);
    m.started_at = 1000;
    m.ended_at = 1060;
    ASSERT(rollout_meta_duration_s(&m) == 60);
    return 0;
}
static int test_cost_accumulation(void) {
    rollout_meta_t m;
    rollout_meta_init(&m);
    rollout_meta_set_session(&m, "cost_test", "gpt-5.4", "/tmp");
    rollout_meta_record_turn(&m, 2, 500, 200, 0.03);
    rollout_meta_record_turn(&m, 1, 300, 100, 0.01);
    rollout_meta_record_turn(&m, 0, 100, 50, 0.005);
    ASSERT_INT_EQ(m.turn_count, 3);
    ASSERT_INT_EQ(m.input_tokens, 900);
    ASSERT_INT_EQ(m.output_tokens, 350);
    ASSERT(m.cost > 0.04 && m.cost < 0.05);
    return 0;
}
static int test_roundtrip(void) {
    rollout_meta_t m;
    rollout_meta_init(&m);
    rollout_meta_set_session(&m, "rt1", "gpt-5.4", "/home/user/proj");
    rollout_meta_record_turn(&m, 5, 2000, 1000, 0.10);
    rollout_meta_record_compact(&m);
    rollout_meta_record_compact(&m);
    rollout_meta_record_compact(&m);
    rollout_meta_finalize(&m, "completed feature X");
    char *json = rollout_meta_serialize(&m);
    ASSERT_NOT_NULL(json);
    rollout_meta_t m2;
    ASSERT_INT_EQ(rollout_meta_deserialize(&m2, json), 0);
    ASSERT_STR_EQ(m2.session_id, "rt1");
    ASSERT_STR_EQ(m2.workspace, "/home/user/proj");
    ASSERT_INT_EQ(m2.compact_count, 3);
    ASSERT_INT_EQ(m2.tool_call_count, 5);
    ASSERT_STR_CONTAINS(m2.summary, "completed feature X");
    free(json);
    return 0;
}

static int test_source_tracking(void) {
    rollout_meta_t m;
    rollout_meta_init(&m);
    rollout_meta_set_source(&m, SESSION_SRC_SERVER);
    ASSERT_INT_EQ((int)m.source, (int)SESSION_SRC_SERVER);

    char *json = rollout_meta_serialize(&m);
    ASSERT_NOT_NULL(json);
    ASSERT_STR_CONTAINS(json, "\"source\":\"server\"");

    rollout_meta_t m2;
    ASSERT_INT_EQ(rollout_meta_deserialize(&m2, json), 0);
    ASSERT_INT_EQ((int)m2.source, (int)SESSION_SRC_SERVER);
    free(json);
    return 0;
}
static int test_parent_session(void) {
    rollout_meta_t m;
    rollout_meta_init(&m);
    rollout_meta_set_session(&m, "child-1", "gpt-5.4", "/tmp");
    rollout_meta_set_parent(&m, "parent-0");

    char *json = rollout_meta_serialize(&m);
    ASSERT_NOT_NULL(json);
    ASSERT_STR_CONTAINS(json, "\"parent_session\":\"parent-0\"");

    rollout_meta_t m2;
    ASSERT_INT_EQ(rollout_meta_deserialize(&m2, json), 0);
    ASSERT_STR_EQ(m2.parent_session, "parent-0");
    free(json);
    return 0;
}
static int test_source_name_parse(void) {
    ASSERT_STR_EQ(session_source_name(SESSION_SRC_CLI), "cli");
    ASSERT_STR_EQ(session_source_name(SESSION_SRC_SERVER), "server");
    ASSERT_STR_EQ(session_source_name(SESSION_SRC_SDK), "sdk");
    ASSERT_STR_EQ(session_source_name(SESSION_SRC_REMOTE), "remote");
    ASSERT_STR_EQ(session_source_name(SESSION_SRC_AGENT), "agent");
    ASSERT_INT_EQ((int)session_source_parse("server"), (int)SESSION_SRC_SERVER);
    ASSERT_INT_EQ((int)session_source_parse("sdk"), (int)SESSION_SRC_SDK);
    ASSERT_INT_EQ((int)session_source_parse(NULL), (int)SESSION_SRC_CLI);
    ASSERT_INT_EQ((int)session_source_parse("unknown"), (int)SESSION_SRC_CLI);
    return 0;
}

static int test_file_persist(void) {
    rollout_meta_t m;
    rollout_meta_init(&m);
    rollout_meta_set_session(&m, "file-rt", "gpt-5.4", "/ws");
    rollout_meta_record_turn(&m, 3, 800, 400, 0.05);
    rollout_meta_record_compact(&m);
    rollout_meta_finalize(&m, "persist test");

    char *json = rollout_meta_serialize(&m);
    ASSERT_NOT_NULL(json);

    char path[128];
    snprintf(path, sizeof(path), "/tmp/nevercode_meta_%d.json", (int)getpid());
    FILE *f = fopen(path, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "%s\n", json);
    ASSERT_INT_EQ(fflush(f), 0);
    fclose(f);

    size_t sz = 0;
    char *loaded = strbuf_read_file(path, &sz);
    unlink(path);
    ASSERT_NOT_NULL(loaded);
    ASSERT(sz > 0);

    rollout_meta_t m2;
    ASSERT_INT_EQ(rollout_meta_deserialize(&m2, loaded), 0);
    ASSERT_STR_EQ(m2.session_id, "file-rt");
    ASSERT_INT_EQ(m2.compact_count, 1);
    ASSERT_INT_EQ(m2.tool_call_count, 3);
    ASSERT_STR_CONTAINS(m2.summary, "persist test");
    free(json);
    free(loaded);
    return 0;
}

int test_rollout_metadata_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("rollout_metadata");
    RUN_TEST(test_init);
    RUN_TEST(test_record);
    RUN_TEST(test_compact);
    RUN_TEST(test_serialize);
    RUN_TEST(test_format);
    RUN_TEST(test_duration);
    RUN_TEST(test_cost_accumulation);
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_source_tracking);
    RUN_TEST(test_parent_session);
    RUN_TEST(test_source_name_parse);
    RUN_TEST(test_file_persist);
    TEST_RETURN_FAILED();
}
