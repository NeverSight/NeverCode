#include "test.h"
#include "nevercode.h"
#include "conversation.h"
#include <string.h>
#include <stdio.h>

static int test_auto_mem_set_get(void) {
    memory_ctx_t m;
    memory_init(&m);
    memory_set(&m, "auto_project_uses_", "React and TypeScript");
    const char *val = memory_get(&m, "auto_project_uses_");
    ASSERT_NOT_NULL(val);
    ASSERT(strstr(val, "React") != NULL);
    memory_free(&m);
    return 0;
}

static int test_auto_mem_no_overwrite(void) {
    memory_ctx_t m;
    memory_init(&m);
    memory_set(&m, "auto_test_key", "value1");
    const char *v1 = memory_get(&m, "auto_test_key");
    ASSERT_STR_EQ(v1, "value1");
    memory_free(&m);
    return 0;
}

static int test_auto_mem_format_includes_auto(void) {
    memory_ctx_t m;
    memory_init(&m);
    memory_set(&m, "auto_framework_is_", "Vue.js with Vuex");
    char *prompt = memory_format_for_prompt(&m);
    ASSERT_NOT_NULL(prompt);
    ASSERT(strstr(prompt, "Vue.js") != NULL);
    free(prompt);
    memory_free(&m);
    return 0;
}

static int test_share_conversation_format(void) {
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "Hello");
    message_t *am = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_text(am, "Hi there!");

    strbuf_t md;
    strbuf_init(&md);
    strbuf_append_str(&md, "# Nevercode Session\n\n");
    for (int i = 0; i < conv.len; i++) {
        const message_t *m = &conv.msgs[i];
        if (m->role == ROLE_USER)
            strbuf_append_str(&md, "## User\n\n");
        else if (m->role == ROLE_ASSISTANT)
            strbuf_append_str(&md, "## Assistant\n\n");
        for (int j = 0; j < m->num_blocks; j++) {
            if (m->blocks[j].type == CONTENT_TEXT && m->blocks[j].text) {
                strbuf_append_str(&md, m->blocks[j].text);
                strbuf_append_str(&md, "\n\n");
            }
        }
    }
    ASSERT_NOT_NULL(md.data);
    ASSERT(strstr(md.data, "## User") != NULL);
    ASSERT(strstr(md.data, "## Assistant") != NULL);
    ASSERT(strstr(md.data, "Hello") != NULL);
    ASSERT(strstr(md.data, "Hi there!") != NULL);
    strbuf_free(&md);
    conversation_free(&conv);
    return 0;
}

static int test_share_with_tools(void) {
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "list files");
    message_t *am = conversation_add(&conv, ROLE_ASSISTANT);
    message_add_tool_use(am, "tc1", "Bash", NULL);
    message_t *tm = conversation_add(&conv, ROLE_TOOL);
    message_add_tool_result(tm, "tc1", "file1.c\nfile2.c", 0);

    ASSERT_INT_EQ(conv.len, 3);
    ASSERT(conv.msgs[1].blocks[0].type == CONTENT_TOOL_USE);
    ASSERT(conv.msgs[2].blocks[0].type == CONTENT_TOOL_RESULT);
    conversation_free(&conv);
    return 0;
}

static int test_share_with_image_block(void) {
    conversation_t conv;
    conversation_init(&conv);
    message_t *um = conversation_add_user_multimodal(&conv);
    message_add_text(um, "What is this?");
    message_add_image(um, "iVBOR...", "image/png");
    ASSERT_INT_EQ(conv.len, 1);
    ASSERT_INT_EQ(conv.msgs[0].num_blocks, 2);
    ASSERT(conv.msgs[0].blocks[1].type == CONTENT_IMAGE);
    conversation_free(&conv);
    return 0;
}

static int test_multimodal_message_free(void) {
    message_t m;
    message_init(&m, ROLE_USER);
    message_add_text(&m, "test");
    message_add_image(&m, "base64data", "image/jpeg");
    message_add_text(&m, "more text");
    ASSERT_INT_EQ(m.num_blocks, 3);
    message_free(&m);
    ASSERT_INT_EQ(m.num_blocks, 0);
    return 0;
}

int test_auto_memory_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("auto_memory");
    RUN_TEST(test_auto_mem_set_get);
    RUN_TEST(test_auto_mem_no_overwrite);
    RUN_TEST(test_auto_mem_format_includes_auto);
    RUN_TEST(test_share_conversation_format);
    RUN_TEST(test_share_with_tools);
    RUN_TEST(test_share_with_image_block);
    RUN_TEST(test_multimodal_message_free);
    TEST_RETURN_FAILED();
}
