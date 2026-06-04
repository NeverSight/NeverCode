#include "test.h"
#include "nevercode.h"
static message_t mk(message_role_t role, const char *text) {
    message_t m;
    memset(&m, 0, sizeof(m));
    m.role = role;
    m.blocks = calloc(1, sizeof(content_block_t));
    m.blocks[0].type = CONTENT_TEXT;
    m.blocks[0].text = nc_xstrdup(text);
    m.num_blocks = 1;
    m.cap_blocks = 1;
    return m;
}
static void fm(message_t *m) {
    for (int i = 0; i < m->num_blocks; i++) {
        free(m->blocks[i].text);
        free(m->blocks[i].tool_use_id);
        free(m->blocks[i].tool_name);
    }
    free(m->blocks);
}
static void ft(turn_item_t *t) {
    free(t->id);
    free(t->text);
    free(t->tool_name);
    free(t->phase);
}
static int t1(void) {
    message_t m = mk(ROLE_USER, "Hello");
    turn_item_t ti;
    ASSERT_INT_EQ(event_map_message(&m, 1, &ti), 0);
    ASSERT(ti.kind == TURN_ITEM_USER_MSG);
    ASSERT_STR_EQ(ti.text, "Hello");
    ft(&ti);
    fm(&m);
    return 0;
}
static int t2(void) {
    message_t m = mk(ROLE_ASSISTANT, "I help");
    turn_item_t ti;
    event_map_message(&m, 2, &ti);
    ASSERT(ti.kind == TURN_ITEM_AGENT_MSG);
    ft(&ti);
    fm(&m);
    return 0;
}
static int t3(void) {
    ASSERT_TRUE(event_map_is_contextual_text("<environment_context>x"));
    ASSERT_FALSE(event_map_is_contextual_text("normal"));
    return 0;
}
static int t4(void) {
    message_t m = mk(ROLE_USER, "<environment_context>x");
    turn_item_t ti;
    event_map_message(&m, 1, &ti);
    ASSERT(ti.kind == TURN_ITEM_NONE);
    ft(&ti);
    fm(&m);
    return 0;
}
static int t5(void) {
    message_t ms[4];
    ms[0] = mk(ROLE_USER, "a");
    ms[1] = mk(ROLE_ASSISTANT, "b");
    ms[2] = mk(ROLE_USER, "c");
    ms[3] = mk(ROLE_ASSISTANT, "d");
    turn_item_t it[8];
    int n = event_map_conversation(ms, 4, it, 8);
    ASSERT_INT_EQ(n, 4);
    for (int i = 0; i < n; i++)
        ft(&it[i]);
    for (int i = 0; i < 4; i++)
        fm(&ms[i]);
    return 0;
}
static int t6(void) {
    message_t m1 = mk(ROLE_USER, "hi");
    ASSERT_TRUE(event_map_is_user_boundary(&m1));
    fm(&m1);
    return 0;
}
static int t7(void) {
    ASSERT_STR_EQ(turn_item_kind_name(TURN_ITEM_NONE), "none");
    ASSERT_STR_EQ(turn_item_kind_name(TURN_ITEM_USER_MSG), "user");
    return 0;
}
static int t8(void) {
    turn_item_t ti;
    memset(&ti, 0, sizeof(ti));
    ti.kind = TURN_ITEM_AGENT_MSG;
    ti.turn_id = 3;
    ti.text = nc_xstrdup("hi");
    char *f = event_map_format_items(&ti, 1);
    ASSERT_NOT_NULL(f);
    ASSERT_STR_CONTAINS(f, "agent");
    free(f);
    free(ti.text);
    return 0;
}
int test_event_mapping_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("event_mapping");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    TEST_RETURN_FAILED();
}
