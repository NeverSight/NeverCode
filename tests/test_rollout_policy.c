#include "test.h"
#include "nevercode.h"
#include "defaults.h"
#include <stdlib.h>
#include <string.h>

typedef enum { RP_PERSIST = 0, RP_SKIP = 1, RP_REDACT = 2, RP_SUMMARIZE = 3 } rollout_persist_t;
typedef enum {
    RI_USER_MSG = 0,
    RI_ASSISTANT = 1,
    RI_TOOL_CALL = 2,
    RI_TOOL_RESULT = 3,
    RI_SYSTEM_MSG = 4,
    RI_COMPACT_MARK = 5,
    RI_TURN_BOUNDARY = 6,
    RI_ASSESSMENT = 7,
    RI_COLLAB_EVENT = 8
} rollout_item_kind_t;

rollout_persist_t rollout_policy_decide(rollout_item_kind_t kind, int content_len);
rollout_persist_t rollout_policy_decide_mode(rollout_item_kind_t kind, int content_len,
                                             int persist_mode);
bool rollout_policy_should_persist(rollout_item_kind_t kind, int content_len);
bool rollout_policy_should_persist_mode(rollout_item_kind_t kind, int content_len,
                                        int persist_mode);
bool rollout_policy_needs_summary(rollout_item_kind_t kind, int content_len);
const char *rollout_persist_name(rollout_persist_t p);
const char *rollout_item_kind_name(rollout_item_kind_t k);
int rollout_policy_set_rule(rollout_item_kind_t kind, rollout_persist_t decision);
char *rollout_policy_format(void);

static int test_user_msg(void) {
    ASSERT_INT_EQ(rollout_policy_decide(RI_USER_MSG, 100), RP_PERSIST);
    ASSERT_TRUE(rollout_policy_should_persist(RI_USER_MSG, 100));
    return 0;
}
static int test_system_skip(void) {
    ASSERT_INT_EQ(rollout_policy_decide(RI_SYSTEM_MSG, 100), RP_SKIP);
    ASSERT_FALSE(rollout_policy_should_persist(RI_SYSTEM_MSG, 100));
    return 0;
}
static int test_tool_result_small(void) {
    ASSERT_TRUE(rollout_policy_should_persist(RI_TOOL_RESULT, 1000));
    ASSERT_FALSE(rollout_policy_needs_summary(RI_TOOL_RESULT, 1000));
    return 0;
}
static int test_tool_result_large(void) {
    ASSERT_TRUE(rollout_policy_should_persist(RI_TOOL_RESULT, 100000));
    ASSERT_TRUE(rollout_policy_needs_summary(RI_TOOL_RESULT, 100000));
    return 0;
}
static int test_persist_names(void) {
    ASSERT_STR_EQ(rollout_persist_name(RP_PERSIST), "persist");
    ASSERT_STR_EQ(rollout_persist_name(RP_SKIP), "skip");
    return 0;
}
static int test_kind_names(void) {
    ASSERT_STR_EQ(rollout_item_kind_name(RI_USER_MSG), "user_message");
    ASSERT_STR_EQ(rollout_item_kind_name(RI_TOOL_CALL), "tool_call");
    ASSERT_STR_EQ(rollout_item_kind_name(RI_ASSESSMENT), "assessment");
    ASSERT_STR_EQ(rollout_item_kind_name(RI_COLLAB_EVENT), "collab_event");
    ASSERT_STR_EQ(rollout_item_kind_name(RI_TURN_BOUNDARY), "turn_boundary");
    return 0;
}
static int test_format(void) {
    char *s = rollout_policy_format();
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Rollout policy");
    ASSERT_STR_CONTAINS(s, "limited");
    ASSERT_STR_CONTAINS(s, "extended");
    free(s);
    return 0;
}
static int test_mode_limited_skips_extended(void) {
    ASSERT_INT_EQ(rollout_policy_decide_mode(RI_ASSESSMENT, 100, RP_MODE_LIMITED), RP_SKIP);
    ASSERT_INT_EQ(rollout_policy_decide_mode(RI_COLLAB_EVENT, 100, RP_MODE_LIMITED), RP_SKIP);
    ASSERT_INT_EQ(rollout_policy_decide_mode(RI_TOOL_RESULT, 100000, RP_MODE_LIMITED), RP_SKIP);
    return 0;
}
static int test_mode_extended_includes_all(void) {
    ASSERT_INT_EQ(rollout_policy_decide_mode(RI_ASSESSMENT, 100, RP_MODE_EXTENDED), RP_PERSIST);
    ASSERT_INT_EQ(rollout_policy_decide_mode(RI_COLLAB_EVENT, 100, RP_MODE_EXTENDED), RP_PERSIST);
    ASSERT_TRUE(rollout_policy_should_persist_mode(RI_TOOL_RESULT, 100000, RP_MODE_EXTENDED));
    return 0;
}
static int test_mode_limited_keeps_core(void) {
    ASSERT_INT_EQ(rollout_policy_decide_mode(RI_USER_MSG, 100, RP_MODE_LIMITED), RP_PERSIST);
    ASSERT_INT_EQ(rollout_policy_decide_mode(RI_ASSISTANT, 100, RP_MODE_LIMITED), RP_PERSIST);
    ASSERT_INT_EQ(rollout_policy_decide_mode(RI_TOOL_CALL, 100, RP_MODE_LIMITED), RP_PERSIST);
    ASSERT_INT_EQ(rollout_policy_decide_mode(RI_COMPACT_MARK, 100, RP_MODE_LIMITED), RP_PERSIST);
    ASSERT_INT_EQ(rollout_policy_decide_mode(RI_TURN_BOUNDARY, 100, RP_MODE_LIMITED), RP_PERSIST);
    return 0;
}

int test_rollout_policy_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("rollout_policy");
    RUN_TEST(test_user_msg);
    RUN_TEST(test_system_skip);
    RUN_TEST(test_tool_result_small);
    RUN_TEST(test_tool_result_large);
    RUN_TEST(test_persist_names);
    RUN_TEST(test_kind_names);
    RUN_TEST(test_format);
    RUN_TEST(test_mode_limited_skips_extended);
    RUN_TEST(test_mode_extended_includes_all);
    RUN_TEST(test_mode_limited_keeps_core);
    TEST_RETURN_FAILED();
}
