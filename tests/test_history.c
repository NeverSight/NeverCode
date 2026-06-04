#include "test.h"
#include "nevercode.h"
#include "conversation.h"
#include <unistd.h>
#include <sys/stat.h>

extern int history_save(const conversation_t *c, const char *id);
extern int history_load(conversation_t *c, const char *id);
extern char *history_gen_session_id(void);
extern char *history_find_latest(void);
extern int history_list(char **out);

static int test_session_id_gen(void) {
    char *id1 = history_gen_session_id();
    ASSERT_NOT_NULL(id1);
    ASSERT(strlen(id1) >= 8);

    char *id2 = history_gen_session_id();
    ASSERT_NOT_NULL(id2);
    ASSERT(strcmp(id1, id2) != 0);

    free(id1);
    free(id2);
    return 0;
}

static int test_save_and_load(void) {
    conversation_t c;
    conversation_init(&c);
    c.system_prompt = nc_xstrdup("Test system prompt");
    conversation_add_user_text(&c, "Hello from save test");

    message_t *am = conversation_add(&c, ROLE_ASSISTANT);
    message_add_text(am, "Hello back!");

    int rc = history_save(&c, "test_save_load_unit");
    ASSERT_INT_EQ(rc, 0);

    conversation_t c2;
    conversation_init(&c2);
    rc = history_load(&c2, "test_save_load_unit");
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(c2.len, 2);
    ASSERT_INT_EQ(c2.msgs[0].role, ROLE_USER);
    ASSERT_STR_EQ(c2.msgs[0].blocks[0].text, "Hello from save test");
    ASSERT_INT_EQ(c2.msgs[1].role, ROLE_ASSISTANT);
    ASSERT_STR_EQ(c2.msgs[1].blocks[0].text, "Hello back!");
    ASSERT_NOT_NULL(c2.system_prompt);
    ASSERT(strstr(c2.system_prompt, "Test system prompt") != NULL);

    conversation_free(&c);
    conversation_free(&c2);

    char path[4096];
    const char *home = getenv(ENV_HOME);
    snprintf(path, sizeof(path), "%s/.nevercode/history/test_save_load_unit.json", home);
    unlink(path);
    return 0;
}

static int test_save_tool_messages(void) {
    conversation_t c;
    conversation_init(&c);
    conversation_add_user_text(&c, "Run ls");

    message_t *am = conversation_add(&c, ROLE_ASSISTANT);
    cJSON *input = cJSON_CreateObject();
    cJSON_AddStringToObject(input, "command", "ls");
    message_add_tool_use(am, "tc_test_1", "Bash", input);
    cJSON_Delete(input);

    message_t *tm = conversation_add(&c, ROLE_TOOL);
    message_add_tool_result(tm, "tc_test_1", "file1.txt\nfile2.txt", 0);

    int rc = history_save(&c, "test_tool_hist");
    ASSERT_INT_EQ(rc, 0);

    conversation_t c2;
    conversation_init(&c2);
    rc = history_load(&c2, "test_tool_hist");
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(c2.len, 3);
    ASSERT_INT_EQ(c2.msgs[1].blocks[0].type, CONTENT_TOOL_USE);
    ASSERT_STR_EQ(c2.msgs[1].blocks[0].tool_name, "Bash");
    ASSERT_INT_EQ(c2.msgs[2].blocks[0].type, CONTENT_TOOL_RESULT);
    ASSERT_STR_EQ(c2.msgs[2].blocks[0].text, "file1.txt\nfile2.txt");

    conversation_free(&c);
    conversation_free(&c2);

    char path[4096];
    const char *home = getenv(ENV_HOME);
    snprintf(path, sizeof(path), "%s/.nevercode/history/test_tool_hist.json", home);
    unlink(path);
    return 0;
}

static int test_load_nonexistent(void) {
    conversation_t c;
    conversation_init(&c);
    int rc = history_load(&c, "nonexistent_session_xyz_12345");
    ASSERT(rc != 0);
    conversation_free(&c);
    return 0;
}

static int test_history_listing(void) {
    char *out = NULL;
    int n = history_list(&out);
    ASSERT_NOT_NULL(out);
    (void)n;
    free(out);
    return 0;
}

int test_history_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("history");
    RUN_TEST(test_session_id_gen);
    RUN_TEST(test_save_and_load);
    RUN_TEST(test_save_tool_messages);
    RUN_TEST(test_load_nonexistent);
    RUN_TEST(test_history_listing);
    TEST_RETURN_FAILED();
}
