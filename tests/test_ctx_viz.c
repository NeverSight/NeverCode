#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

static int test_empty(void) {
    ctx_viz_result_t viz;
    ASSERT_INT_EQ(ctx_viz_analyze(&viz, NULL, 0, 128000, 500), 0);
    ASSERT_INT_EQ(viz.count, 1);
    ASSERT(viz.free_tokens > 0);
    return 0;
}
static int test_with_messages(void) {
    message_t msgs[2];
    memset(msgs, 0, sizeof(msgs));
    content_block_t b1 = {.type = CONTENT_TEXT, .text = nc_xstrdup("Hello, world!")};
    content_block_t b2 = {.type = CONTENT_TEXT, .text = nc_xstrdup("I can help with that.")};
    msgs[0].role = ROLE_USER;
    msgs[0].blocks = &b1;
    msgs[0].num_blocks = 1;
    msgs[1].role = ROLE_ASSISTANT;
    msgs[1].blocks = &b2;
    msgs[1].num_blocks = 1;
    ctx_viz_result_t viz;
    ctx_viz_analyze(&viz, msgs, 2, 128000, 1000);
    ASSERT(viz.count >= 3);
    ASSERT(viz.total_tokens > 1000);
    free(b1.text);
    free(b2.text);
    return 0;
}
static int test_render(void) {
    ctx_viz_result_t viz;
    ctx_viz_analyze(&viz, NULL, 0, 100000, 500);
    char *s = ctx_viz_render(&viz);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "Context Window");
    free(s);
    return 0;
}
static int test_render_json(void) {
    ctx_viz_result_t viz;
    ctx_viz_analyze(&viz, NULL, 0, 100000, 500);
    char *s = ctx_viz_render_json(&viz);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "total_tokens");
    free(s);
    return 0;
}
static int test_null(void) {
    ASSERT_INT_EQ(ctx_viz_analyze(NULL, NULL, 0, 0, 0), -1);
    return 0;
}

int test_ctx_viz_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("ctx_viz");
    RUN_TEST(test_empty);
    RUN_TEST(test_with_messages);
    RUN_TEST(test_render);
    RUN_TEST(test_render_json);
    RUN_TEST(test_null);
    TEST_RETURN_FAILED();
}
