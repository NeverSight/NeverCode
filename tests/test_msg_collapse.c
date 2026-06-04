/*
 * test_msg_collapse.c — Tests for message collapse grouping
 */

#include "test.h"
#include "nevercode.h"

static message_t make_tool_msg(const char *tool_name) {
    message_t m;
    memset(&m, 0, sizeof(m));
    m.role = ROLE_ASSISTANT;
    m.blocks = calloc(1, sizeof(content_block_t));
    m.num_blocks = 1;
    m.cap_blocks = 1;
    m.blocks[0].type = CONTENT_TOOL_USE;
    m.blocks[0].tool_name = nc_xstrdup(tool_name);
    return m;
}

static void free_msg(message_t *m) {
    for (int i = 0; i < m->num_blocks; i++) {
        free(m->blocks[i].text);
        free(m->blocks[i].tool_name);
        free(m->blocks[i].tool_use_id);
        cJSON_Delete(m->blocks[i].tool_input);
        free(m->blocks[i].image_data);
        free(m->blocks[i].image_media_type);
    }
    free(m->blocks);
}

static int test_single_read_no_collapse(void) {
    message_t msgs[1];
    msgs[0] = make_tool_msg("file_read");

    msg_collapse_entry_t out[4];
    int n = msg_collapse_scan(msgs, 1, out, 4);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(out[0].kind, MSG_COLLAPSE_PASSTHROUGH);
    ASSERT_INT_EQ(out[0].count, 1);

    free_msg(&msgs[0]);
    return 0;
}

static int test_consecutive_reads_collapse(void) {
    message_t msgs[3];
    msgs[0] = make_tool_msg("file_read");
    msgs[1] = make_tool_msg("file_read");
    msgs[2] = make_tool_msg("grep");

    msg_collapse_entry_t out[4];
    int n = msg_collapse_scan(msgs, 3, out, 4);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(out[0].kind, MSG_COLLAPSE_READ_SEARCH);
    ASSERT_INT_EQ(out[0].reads, 2);
    ASSERT_INT_EQ(out[0].searches, 1);
    ASSERT_INT_EQ(out[0].count, 3);

    for (int i = 0; i < 3; i++)
        free_msg(&msgs[i]);
    return 0;
}

static int test_mixed_breaks_group(void) {
    message_t msgs[4];
    msgs[0] = make_tool_msg("file_read");
    msgs[1] = make_tool_msg("bash");
    msgs[2] = make_tool_msg("file_read");
    msgs[3] = make_tool_msg("file_read");

    msg_collapse_entry_t out[8];
    int n = msg_collapse_scan(msgs, 4, out, 8);
    ASSERT_INT_EQ(n, 3);
    ASSERT_INT_EQ(out[0].kind, MSG_COLLAPSE_PASSTHROUGH);
    ASSERT_INT_EQ(out[1].kind, MSG_COLLAPSE_PASSTHROUGH);
    ASSERT_INT_EQ(out[2].kind, MSG_COLLAPSE_READ_SEARCH);
    ASSERT_INT_EQ(out[2].reads, 2);

    for (int i = 0; i < 4; i++)
        free_msg(&msgs[i]);
    return 0;
}

static int test_format_output(void) {
    msg_collapse_entry_t e;
    e.kind = MSG_COLLAPSE_READ_SEARCH;
    e.start = 0;
    e.count = 5;
    e.reads = 3;
    e.searches = 2;
    e.lists = 0;
    nc_strlcpy(e.path_hint, "5 files", sizeof(e.path_hint));

    char *s = msg_collapse_format(&e);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_CONTAINS(s, "3 reads");
    ASSERT_STR_CONTAINS(s, "2 searches");
    ASSERT_STR_CONTAINS(s, "5 files");
    free(s);
    return 0;
}

static int test_null_safety(void) {
    ASSERT_INT_EQ(msg_collapse_scan(NULL, 0, NULL, 0), 0);
    ASSERT_NULL(msg_collapse_format(NULL));

    msg_collapse_entry_t pass;
    memset(&pass, 0, sizeof(pass));
    pass.kind = MSG_COLLAPSE_PASSTHROUGH;
    ASSERT_NULL(msg_collapse_format(&pass));
    return 0;
}

int test_msg_collapse_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("msg_collapse");
    RUN_TEST(test_single_read_no_collapse);
    RUN_TEST(test_consecutive_reads_collapse);
    RUN_TEST(test_mixed_breaks_group);
    RUN_TEST(test_format_output);
    RUN_TEST(test_null_safety);
    return _t_failed;
}
