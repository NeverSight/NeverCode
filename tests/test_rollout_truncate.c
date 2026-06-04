/*
 * test_rollout_truncate.c — Tests for rollout event truncation/pruning.
 */
#include "test.h"
#include "nevercode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cJSON *make_event(const char *type, int turn) {
    cJSON *ev = cJSON_CreateObject();
    cJSON_AddStringToObject(ev, "type", type);
    cJSON_AddNumberToObject(ev, "turn", turn);
    return ev;
}

static cJSON *make_events(int n_user_turns, int msgs_per_turn) {
    cJSON *arr = cJSON_CreateArray();
    int turn = 0;
    for (int i = 0; i < n_user_turns; i++) {
        turn++;
        cJSON_AddItemToArray(arr, make_event("user", turn));
        for (int j = 0; j < msgs_per_turn; j++)
            cJSON_AddItemToArray(arr, make_event("assistant", turn));
    }
    return arr;
}

static int test_boundaries_basic(void) {
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, make_event("user", 1));
    cJSON_AddItemToArray(arr, make_event("assistant", 1));
    cJSON_AddItemToArray(arr, make_event("user", 2));
    cJSON_AddItemToArray(arr, make_event("assistant", 2));
    cJSON_AddItemToArray(arr, make_event("user", 3));

    int positions[64];
    int n = rollout_truncate_find_user_boundaries(arr, positions, 64);
    ASSERT_INT_EQ(n, 3);
    ASSERT_INT_EQ(positions[0], 0);
    ASSERT_INT_EQ(positions[1], 2);
    ASSERT_INT_EQ(positions[2], 4);

    cJSON_Delete(arr);
    return 0;
}

static int test_boundaries_no_user(void) {
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, make_event("assistant", 1));
    cJSON_AddItemToArray(arr, make_event("tool_call", 1));
    cJSON_AddItemToArray(arr, make_event("tool_result", 1));

    int positions[64];
    int n = rollout_truncate_find_user_boundaries(arr, positions, 64);
    ASSERT_INT_EQ(n, 0);

    cJSON_Delete(arr);
    return 0;
}

static int test_cut_point_below_threshold(void) {
    cJSON *arr = make_events(5, 1);
    int cut = rollout_truncate_find_cut_point(arr, ROLLOUT_TRUNCATE_KEEP_TURNS);
    ASSERT_INT_EQ(cut, 0);
    cJSON_Delete(arr);
    return 0;
}

static int test_cut_point_above_threshold(void) {
    int n_turns = ROLLOUT_TRUNCATE_THRESHOLD + 50;
    cJSON *arr = make_events(n_turns, 0);
    ASSERT_GT(cJSON_GetArraySize(arr), ROLLOUT_TRUNCATE_THRESHOLD);

    int cut = rollout_truncate_find_cut_point(arr, ROLLOUT_TRUNCATE_KEEP_TURNS);
    ASSERT_GT(cut, 0);

    cJSON_Delete(arr);
    return 0;
}

static int test_truncate_preserves_recent(void) {
    int n_turns = ROLLOUT_TRUNCATE_THRESHOLD + 100;
    cJSON *arr = make_events(n_turns, 0);

    cJSON *trimmed = rollout_truncate_events(arr, ROLLOUT_TRUNCATE_KEEP_TURNS);
    ASSERT_NOT_NULL(trimmed);

    int new_size = cJSON_GetArraySize(trimmed);
    ASSERT(new_size < cJSON_GetArraySize(arr));
    ASSERT(new_size > 0);

    /* Last event should still be a user turn at the last turn number */
    cJSON *last = cJSON_GetArrayItem(trimmed, new_size - 1);
    const cJSON *turn = cJSON_GetObjectItem(last, "turn");
    ASSERT_INT_EQ((int)turn->valuedouble, n_turns);

    cJSON_Delete(trimmed);
    cJSON_Delete(arr);
    return 0;
}

static int test_truncate_adds_marker(void) {
    int n_turns = ROLLOUT_TRUNCATE_THRESHOLD + 50;
    cJSON *arr = make_events(n_turns, 0);

    cJSON *trimmed = rollout_truncate_events(arr, ROLLOUT_TRUNCATE_KEEP_TURNS);
    ASSERT_NOT_NULL(trimmed);

    cJSON *first = cJSON_GetArrayItem(trimmed, 0);
    const cJSON *type = cJSON_GetObjectItem(first, "type");
    ASSERT_STR_EQ(type->valuestring, "compact");
    const cJSON *trunc_n = cJSON_GetObjectItem(first, "truncated_events");
    ASSERT(trunc_n != NULL);
    ASSERT_GT((int)trunc_n->valuedouble, 0);

    cJSON_Delete(trimmed);
    cJSON_Delete(arr);
    return 0;
}

static int test_no_truncation_when_small(void) {
    cJSON *arr = make_events(10, 1);
    cJSON *trimmed = rollout_truncate_events(arr, ROLLOUT_TRUNCATE_KEEP_TURNS);
    ASSERT_NOT_NULL(trimmed);
    ASSERT_INT_EQ(cJSON_GetArraySize(trimmed), cJSON_GetArraySize(arr));
    cJSON_Delete(trimmed);
    cJSON_Delete(arr);
    return 0;
}

static int test_null_inputs(void) {
    ASSERT_INT_EQ(rollout_truncate_find_user_boundaries(NULL, NULL, 0), 0);
    ASSERT_INT_EQ(rollout_truncate_find_cut_point(NULL, 10), 0);
    ASSERT_NULL(rollout_truncate_events(NULL, 10));
    ASSERT_INT_EQ(rollout_truncate_file(NULL, 10), -1);
    ASSERT_INT_EQ(rollout_truncate_check_needed(NULL), 0);
    return 0;
}

static int test_checkpoint_preservation(void) {
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < ROLLOUT_TRUNCATE_THRESHOLD + 50; i++) {
        if (i == 500)
            cJSON_AddItemToArray(arr, make_event("checkpoint", i));
        else if (i % 2 == 0)
            cJSON_AddItemToArray(arr, make_event("user", i));
        else
            cJSON_AddItemToArray(arr, make_event("assistant", i));
    }

    cJSON *trimmed = rollout_truncate_events(arr, ROLLOUT_TRUNCATE_KEEP_TURNS);
    ASSERT_NOT_NULL(trimmed);
    ASSERT(cJSON_GetArraySize(trimmed) < cJSON_GetArraySize(arr));

    cJSON_Delete(trimmed);
    cJSON_Delete(arr);
    return 0;
}

int test_rollout_truncate_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("rollout_truncate");
    RUN_TEST(test_boundaries_basic);
    RUN_TEST(test_boundaries_no_user);
    RUN_TEST(test_cut_point_below_threshold);
    RUN_TEST(test_cut_point_above_threshold);
    RUN_TEST(test_truncate_preserves_recent);
    RUN_TEST(test_truncate_adds_marker);
    RUN_TEST(test_no_truncation_when_small);
    RUN_TEST(test_null_inputs);
    RUN_TEST(test_checkpoint_preservation);
    TEST_RETURN_FAILED();
}
