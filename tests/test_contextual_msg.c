#include "test.h"
#include "nevercode.h"
static message_t mk(message_role_t r, const char *t) {
    message_t m;
    memset(&m, 0, sizeof(m));
    m.role = r;
    m.blocks = calloc(1, sizeof(content_block_t));
    m.blocks[0].type = CONTENT_TEXT;
    m.blocks[0].text = nc_xstrdup(t);
    m.num_blocks = 1;
    m.cap_blocks = 1;
    return m;
}
static void fm(message_t *m) {
    for (int i = 0; i < m->num_blocks; i++)
        free(m->blocks[i].text);
    free(m->blocks);
}
static int t1(void) {
    ASSERT(ctx_frag_classify("<environment_context>x") == CTX_FRAG_ENV);
    return 0;
}
static int t2(void) {
    ASSERT(ctx_frag_classify("<agents_md>x") == CTX_FRAG_INSTRUCTIONS);
    return 0;
}
static int t3(void) {
    ASSERT(ctx_frag_classify("normal") == CTX_FRAG_NONE);
    return 0;
}
static int t4(void) {
    ASSERT_TRUE(ctx_frag_exclude_memory("<agents_md>x"));
    ASSERT_FALSE(ctx_frag_exclude_memory("<environment_context>x"));
    return 0;
}
static int t5(void) {
    message_t m1 = mk(ROLE_USER, "<agents_md>r");
    ASSERT_TRUE(ctx_msg_is_contextual(&m1));
    fm(&m1);
    return 0;
}
static int t6(void) {
    message_t m1 = mk(ROLE_USER, "hi");
    ASSERT_TRUE(ctx_msg_is_user_boundary(&m1));
    fm(&m1);
    return 0;
}
static int t7(void) {
    message_t m;
    memset(&m, 0, sizeof(m));
    m.role = ROLE_USER;
    m.blocks = calloc(2, sizeof(content_block_t));
    m.num_blocks = 2;
    m.cap_blocks = 2;
    m.blocks[0].type = CONTENT_TEXT;
    m.blocks[0].text = nc_xstrdup("<agents_md>r");
    m.blocks[1].type = CONTENT_TEXT;
    m.blocks[1].text = nc_xstrdup("build app");
    strbuf_t sb;
    strbuf_init(&sb);
    int k = ctx_msg_filter_content(&m, &sb);
    ASSERT_INT_EQ(k, 1);
    ASSERT_STR_CONTAINS(sb.data, "build app");
    strbuf_free(&sb);
    free(m.blocks[0].text);
    free(m.blocks[1].text);
    free(m.blocks);
    return 0;
}
static int t8(void) {
    ASSERT_STR_EQ(ctx_frag_kind_name(CTX_FRAG_NONE), "none");
    ASSERT_STR_EQ(ctx_frag_kind_name(CTX_FRAG_ENV), "environment");
    return 0;
}
static int t9(void) {
    message_t ms[2];
    ms[0] = mk(ROLE_USER, "<environment_context>x");
    ms[1] = mk(ROLE_USER, "hi");
    ASSERT_INT_EQ(ctx_frag_count_in_conversation(ms, 2), 1);
    ASSERT_INT_EQ(ctx_frag_user_turn_count(ms, 2), 1);
    fm(&ms[0]);
    fm(&ms[1]);
    return 0;
}
static int t10(void) {
    ASSERT(ctx_frag_classify("<skill_payload>x") == CTX_FRAG_SKILL);
    return 0;
}
int test_contextual_msg_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("contextual_msg");
    RUN_TEST(t1);
    RUN_TEST(t2);
    RUN_TEST(t3);
    RUN_TEST(t4);
    RUN_TEST(t5);
    RUN_TEST(t6);
    RUN_TEST(t7);
    RUN_TEST(t8);
    RUN_TEST(t9);
    RUN_TEST(t10);
    TEST_RETURN_FAILED();
}
