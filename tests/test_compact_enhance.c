#include "test.h"
#include "nevercode.h"
#include "conversation.h"
#include <unistd.h>
#include <sys/stat.h>

/* ---- read_file_tracker tests ---- */

static int test_tracker_init(void) {
    read_file_tracker_t t;
    read_file_tracker_init(&t);
    ASSERT_INT_EQ(t.count, 0);
    read_file_tracker_free(&t);
    return 0;
}

static int test_tracker_add_and_get(void) {
    read_file_tracker_t t;
    read_file_tracker_init(&t);
    read_file_tracker_add(&t, "/tmp/a.c");
    read_file_tracker_add(&t, "/tmp/b.c");
    read_file_tracker_add(&t, "/tmp/c.c");
    ASSERT_INT_EQ(t.count, 3);

    const char *recent[5];
    int n = read_file_tracker_get_recent(&t, recent, 5);
    ASSERT_INT_EQ(n, 3);
    ASSERT_STR_EQ(recent[0], "/tmp/c.c");
    ASSERT_STR_EQ(recent[1], "/tmp/b.c");
    ASSERT_STR_EQ(recent[2], "/tmp/a.c");

    read_file_tracker_free(&t);
    return 0;
}

static int test_tracker_dedup(void) {
    read_file_tracker_t t;
    read_file_tracker_init(&t);
    read_file_tracker_add(&t, "/tmp/a.c");
    read_file_tracker_add(&t, "/tmp/b.c");
    read_file_tracker_add(&t, "/tmp/a.c");
    ASSERT_INT_EQ(t.count, 2);

    const char *recent[5];
    int n = read_file_tracker_get_recent(&t, recent, 5);
    ASSERT_INT_EQ(n, 2);
    ASSERT_STR_EQ(recent[0], "/tmp/a.c");
    ASSERT_STR_EQ(recent[1], "/tmp/b.c");

    read_file_tracker_free(&t);
    return 0;
}

static int test_tracker_null(void) {
    read_file_tracker_t t;
    read_file_tracker_init(&t);
    read_file_tracker_add(&t, NULL);
    read_file_tracker_add(&t, "");
    ASSERT_INT_EQ(t.count, 0);

    const char *recent[5];
    int n = read_file_tracker_get_recent(&t, recent, 0);
    ASSERT_INT_EQ(n, 0);
    n = read_file_tracker_get_recent(NULL, recent, 5);
    ASSERT_INT_EQ(n, 0);

    read_file_tracker_free(&t);
    return 0;
}

static int test_tracker_overflow(void) {
    read_file_tracker_t t;
    read_file_tracker_init(&t);
    for (int i = 0; i < READ_FILE_TRACKER_MAX + 5; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/tmp/f%d.c", i);
        read_file_tracker_add(&t, path);
    }
    ASSERT_INT_EQ(t.count, READ_FILE_TRACKER_MAX);

    const char *recent[2];
    int n = read_file_tracker_get_recent(&t, recent, 2);
    ASSERT_INT_EQ(n, 2);

    char expected[64];
    snprintf(expected, sizeof(expected), "/tmp/f%d.c", READ_FILE_TRACKER_MAX + 4);
    ASSERT_STR_EQ(recent[0], expected);

    read_file_tracker_free(&t);
    return 0;
}

/* ---- message grouping tests ---- */

static int test_group_empty(void) {
    msg_group_t groups[4];
    int n = compact_group_by_api_round(NULL, 0, groups, 4);
    ASSERT_INT_EQ(n, 0);
    return 0;
}

static int test_group_single_round(void) {
    message_t msgs[2];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "hello");
    message_init(&msgs[1], ROLE_ASSISTANT);
    message_add_text(&msgs[1], "hi");

    msg_group_t groups[8];
    int n = compact_group_by_api_round(msgs, 2, groups, 8);
    ASSERT(n >= 1);
    ASSERT_INT_EQ(groups[0].start, 0);

    message_free(&msgs[0]);
    message_free(&msgs[1]);
    return 0;
}

static int test_group_multi_round(void) {
    message_t msgs[6];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "q1");
    message_init(&msgs[1], ROLE_ASSISTANT);
    message_add_text(&msgs[1], "a1");
    message_init(&msgs[2], ROLE_USER);
    message_add_text(&msgs[2], "q2");
    message_init(&msgs[3], ROLE_ASSISTANT);
    message_add_text(&msgs[3], "a2");
    message_init(&msgs[4], ROLE_USER);
    message_add_text(&msgs[4], "q3");
    message_init(&msgs[5], ROLE_ASSISTANT);
    message_add_text(&msgs[5], "a3");

    msg_group_t groups[8];
    int n = compact_group_by_api_round(msgs, 6, groups, 8);
    ASSERT(n >= 2);
    ASSERT_INT_EQ(groups[0].start, 0);

    for (int i = 0; i < 6; i++)
        message_free(&msgs[i]);
    return 0;
}

/* ---- strip images tests ---- */

static int test_strip_no_images(void) {
    message_t msgs[1];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "hello");
    int n = compact_strip_images(msgs, 1);
    ASSERT_INT_EQ(n, 0);
    ASSERT_STR_EQ(msgs[0].blocks[0].text, "hello");
    message_free(&msgs[0]);
    return 0;
}

static int test_strip_with_image(void) {
    message_t msgs[1];
    message_init(&msgs[0], ROLE_USER);
    message_add_image(&msgs[0], "base64data", "image/png");
    ASSERT_INT_EQ(msgs[0].blocks[0].type, CONTENT_IMAGE);
    int n = compact_strip_images(msgs, 1);
    ASSERT_INT_EQ(n, 1);
    ASSERT_INT_EQ(msgs[0].blocks[0].type, CONTENT_TEXT);
    ASSERT_STR_EQ(msgs[0].blocks[0].text, "[image]");
    message_free(&msgs[0]);
    return 0;
}

/* ---- compact_restore_files tests ---- */

static int test_restore_empty_tracker(void) {
    read_file_tracker_t t;
    read_file_tracker_init(&t);
    char *result = compact_restore_files(&t);
    ASSERT_NULL(result);
    read_file_tracker_free(&t);
    return 0;
}

static int test_restore_with_files(void) {
    read_file_tracker_t t;
    read_file_tracker_init(&t);

    char tmpfile[256];
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/nevercode_test_restore_%d.txt", (int)getpid());
    FILE *f = fopen(tmpfile, "w");
    fprintf(f, "This is test content for restore.\nLine 2.\n");
    fclose(f);

    read_file_tracker_add(&t, tmpfile);
    char *result = compact_restore_files(&t);
    ASSERT_NOT_NULL(result);
    ASSERT(strstr(result, "Post-compaction context") != NULL);
    ASSERT(strstr(result, "test content") != NULL);
    free(result);

    unlink(tmpfile);
    read_file_tracker_free(&t);
    return 0;
}

static int test_restore_nonexistent(void) {
    read_file_tracker_t t;
    read_file_tracker_init(&t);
    read_file_tracker_add(&t, "/tmp/this_file_should_not_exist_12345.xyz");
    char *result = compact_restore_files(&t);
    ASSERT_NULL(result);
    read_file_tracker_free(&t);
    return 0;
}

/* ---- resume summary tests ---- */

static int test_resume_empty(void) {
    char *s = generate_resume_summary(NULL, 0, NULL);
    ASSERT_NULL(s);
    return 0;
}

static int test_resume_basic(void) {
    message_t msgs[4];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "Fix the bug in auth.c");
    message_init(&msgs[1], ROLE_ASSISTANT);
    message_add_text(&msgs[1], "I found the issue...");
    message_init(&msgs[2], ROLE_USER);
    message_add_text(&msgs[2], "Now add tests");
    message_init(&msgs[3], ROLE_ASSISTANT);
    message_add_text(&msgs[3], "Done. Added 5 test cases.");

    char *s = generate_resume_summary(msgs, 4, NULL);
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "While you were away") != NULL);
    ASSERT(strstr(s, "Now add tests") != NULL);
    ASSERT(strstr(s, "2 exchanges") != NULL);
    free(s);

    for (int i = 0; i < 4; i++)
        message_free(&msgs[i]);
    return 0;
}

static int test_resume_with_name(void) {
    message_t msgs[2];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "hello");
    message_init(&msgs[1], ROLE_ASSISTANT);
    message_add_text(&msgs[1], "hi");

    char *s = generate_resume_summary(msgs, 2, "Auth Refactor");
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "Session: Auth Refactor") != NULL);
    free(s);

    message_free(&msgs[0]);
    message_free(&msgs[1]);
    return 0;
}

static int test_resume_with_tools(void) {
    message_t msgs[4];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "Read main.c");
    message_init(&msgs[1], ROLE_ASSISTANT);
    cJSON *inp = cJSON_CreateObject();
    cJSON_AddStringToObject(inp, "path", "main.c");
    message_add_tool_use(&msgs[1], "tu1", "Read", inp);
    cJSON_Delete(inp);
    message_init(&msgs[2], ROLE_TOOL);
    message_add_tool_result(&msgs[2], "tu1", "file contents", 0);
    message_init(&msgs[3], ROLE_ASSISTANT);
    message_add_text(&msgs[3], "Here is main.c...");

    char *s = generate_resume_summary(msgs, 4, NULL);
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "tool call") != NULL);
    free(s);

    for (int i = 0; i < 4; i++)
        message_free(&msgs[i]);
    return 0;
}

/* ---- thinking pruning tests ---- */

static int test_prune_thinking_empty(void) {
    int n = compact_prune_thinking(NULL, 0, 3);
    ASSERT_INT_EQ(n, 0);
    return 0;
}

static int test_prune_thinking_no_tags(void) {
    message_t msgs[2];
    message_init(&msgs[0], ROLE_ASSISTANT);
    message_add_text(&msgs[0], "Normal response without thinking.");
    message_init(&msgs[1], ROLE_ASSISTANT);
    message_add_text(&msgs[1], "Another normal response.");

    int n = compact_prune_thinking(msgs, 2, 0);
    ASSERT_INT_EQ(n, 0);
    ASSERT_STR_EQ(msgs[0].blocks[0].text, "Normal response without thinking.");

    message_free(&msgs[0]);
    message_free(&msgs[1]);
    return 0;
}

static int test_prune_thinking_with_tags(void) {
    message_t msgs[3];
    message_init(&msgs[0], ROLE_ASSISTANT);
    message_add_text(&msgs[0], "Before<thinking>internal thoughts here</thinking>After");
    message_init(&msgs[1], ROLE_ASSISTANT);
    message_add_text(&msgs[1], "Recent <thinking>keep this</thinking> response");
    message_init(&msgs[2], ROLE_ASSISTANT);
    message_add_text(&msgs[2], "Latest response");

    int n = compact_prune_thinking(msgs, 3, 2);
    ASSERT_GT(n, 0);
    ASSERT_STR_EQ(msgs[0].blocks[0].text, "BeforeAfter");
    ASSERT(strstr(msgs[1].blocks[0].text, "keep this") != NULL);

    message_free(&msgs[0]);
    message_free(&msgs[1]);
    message_free(&msgs[2]);
    return 0;
}

static int test_prune_thinking_skips_user(void) {
    message_t msgs[2];
    message_init(&msgs[0], ROLE_USER);
    message_add_text(&msgs[0], "User <thinking>not pruned</thinking> message");
    message_init(&msgs[1], ROLE_ASSISTANT);
    message_add_text(&msgs[1], "Response");

    int n = compact_prune_thinking(msgs, 2, 0);
    ASSERT_INT_EQ(n, 0);
    ASSERT(strstr(msgs[0].blocks[0].text, "not pruned") != NULL);

    message_free(&msgs[0]);
    message_free(&msgs[1]);
    return 0;
}

/* ---- memory anchor tests ---- */

static int test_memory_anchor_null(void) {
    char *r = compact_build_memory_anchor(NULL, NULL);
    ASSERT_NULL(r);
    return 0;
}

static int test_memory_anchor_with_memory(void) {
    char *r = compact_build_memory_anchor("- User prefers C over C++", NULL);
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "session_context") != NULL);
    ASSERT(strstr(r, "prefers C") != NULL);
    ASSERT(strstr(r, COMPACT_PREV_SUMMARY_MARKER) != NULL);
    free(r);
    return 0;
}

static int test_memory_anchor_with_summary(void) {
    char *r = compact_build_memory_anchor(NULL, "Worked on auth module, fixed 3 bugs");
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "prior_work_summary") != NULL);
    ASSERT(strstr(r, "auth module") != NULL);
    free(r);
    return 0;
}

static int test_memory_anchor_both(void) {
    char *r = compact_build_memory_anchor("- Prefers tabs", "Refactored config.c");
    ASSERT_NOT_NULL(r);
    ASSERT(strstr(r, "session_context") != NULL);
    ASSERT(strstr(r, "prior_work_summary") != NULL);
    ASSERT(strstr(r, "Prefers tabs") != NULL);
    ASSERT(strstr(r, "config.c") != NULL);
    free(r);
    return 0;
}

int test_compact_enhance_all(void) {
    TEST_SUITE("compact_enhance");
    TEST_COUNTERS;

    RUN_TEST(test_tracker_init);
    RUN_TEST(test_tracker_add_and_get);
    RUN_TEST(test_tracker_dedup);
    RUN_TEST(test_tracker_null);
    RUN_TEST(test_tracker_overflow);
    RUN_TEST(test_group_empty);
    RUN_TEST(test_group_single_round);
    RUN_TEST(test_group_multi_round);
    RUN_TEST(test_strip_no_images);
    RUN_TEST(test_strip_with_image);
    RUN_TEST(test_restore_empty_tracker);
    RUN_TEST(test_restore_with_files);
    RUN_TEST(test_restore_nonexistent);
    RUN_TEST(test_resume_empty);
    RUN_TEST(test_resume_basic);
    RUN_TEST(test_resume_with_name);
    RUN_TEST(test_resume_with_tools);
    RUN_TEST(test_prune_thinking_empty);
    RUN_TEST(test_prune_thinking_no_tags);
    RUN_TEST(test_prune_thinking_with_tags);
    RUN_TEST(test_prune_thinking_skips_user);
    RUN_TEST(test_memory_anchor_null);
    RUN_TEST(test_memory_anchor_with_memory);
    RUN_TEST(test_memory_anchor_with_summary);
    RUN_TEST(test_memory_anchor_both);

    TEST_RETURN_FAILED();
}
