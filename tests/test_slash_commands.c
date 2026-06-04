#include "test.h"
#include "test_helpers.h"
#include "nevercode.h"
#include "repl.h"
#include "conversation.h"
#include "tools.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int test_help_command(void) {
    conversation_t conv;
    conversation_init(&conv);
    config_t cfg;
    config_init(&cfg);
    char *session_id = nc_xstrdup("test-session");
    slash_result_t r = handle_slash_command("/help", &conv, &cfg, &session_id, true);
    ASSERT(r == CMD_CONTINUE);
    free(session_id);
    conversation_free(&conv);
    config_free(&cfg);
    return 0;
}

static int test_version_command(void) {
    conversation_t conv;
    conversation_init(&conv);
    config_t cfg;
    config_init(&cfg);
    char *session_id = nc_xstrdup("test-session");
    slash_result_t r = handle_slash_command("/version", &conv, &cfg, &session_id, true);
    ASSERT(r == CMD_CONTINUE);
    free(session_id);
    conversation_free(&conv);
    config_free(&cfg);
    return 0;
}

static int test_exit_command(void) {
    conversation_t conv;
    conversation_init(&conv);
    config_t cfg;
    config_init(&cfg);
    char *session_id = nc_xstrdup("test-session");
    ASSERT(handle_slash_command("/exit", &conv, &cfg, &session_id, true) == CMD_EXIT);
    ASSERT(handle_slash_command("/quit", &conv, &cfg, &session_id, true) == CMD_EXIT);
    free(session_id);
    conversation_free(&conv);
    config_free(&cfg);
    return 0;
}

static int test_clear_command(void) {
    conversation_t conv;
    conversation_init(&conv);
    config_t cfg;
    config_init(&cfg);
    char *session_id = nc_xstrdup("test-session");
    conversation_add_user_text(&conv, "hello");
    ASSERT(conv.len == 1);
    ASSERT(handle_slash_command("/clear", &conv, &cfg, &session_id, true) == CMD_CONTINUE);
    ASSERT(conv.len == 0);
    free(session_id);
    conversation_free(&conv);
    config_free(&cfg);
    return 0;
}

static int test_plan_mode(void) {
    conversation_t conv;
    conversation_init(&conv);
    config_t cfg;
    config_init(&cfg);
    char *session_id = nc_xstrdup("test-session");
    g_plan_mode = false;
    ASSERT(handle_slash_command("/plan", &conv, &cfg, &session_id, true) == CMD_CONTINUE);
    ASSERT(g_plan_mode == true);
    ASSERT(handle_slash_command("/endplan", &conv, &cfg, &session_id, true) == CMD_CONTINUE);
    ASSERT(g_plan_mode == false);
    free(session_id);
    conversation_free(&conv);
    config_free(&cfg);
    return 0;
}

static int test_brief_mode(void) {
    conversation_t conv;
    conversation_init(&conv);
    config_t cfg;
    config_init(&cfg);
    char *session_id = nc_xstrdup("test-session");
    g_brief_mode = false;
    ASSERT(handle_slash_command("/brief", &conv, &cfg, &session_id, true) == CMD_CONTINUE);
    ASSERT(g_brief_mode == true);
    ASSERT(handle_slash_command("/brief", &conv, &cfg, &session_id, true) == CMD_CONTINUE);
    ASSERT(g_brief_mode == false);
    free(session_id);
    conversation_free(&conv);
    config_free(&cfg);
    return 0;
}

static int test_model_command(void) {
    conversation_t conv;
    conversation_init(&conv);
    config_t cfg;
    config_init(&cfg);
    cfg.model = nc_xstrdup("gpt-4o");
    char *session_id = nc_xstrdup("test-session");
    ASSERT(handle_slash_command("/model gpt-5.4", &conv, &cfg, &session_id, true) == CMD_CONTINUE);
    ASSERT_STR_EQ(cfg.model, "gpt-5.4");
    free(session_id);
    conversation_free(&conv);
    config_free(&cfg);
    return 0;
}

static int test_unknown_command(void) {
    conversation_t conv;
    conversation_init(&conv);
    config_t cfg;
    config_init(&cfg);
    char *session_id = nc_xstrdup("test-session");
    ASSERT(handle_slash_command("/nonexistent", &conv, &cfg, &session_id, true) == CMD_NOT_HANDLED);
    free(session_id);
    conversation_free(&conv);
    config_free(&cfg);
    return 0;
}

static int test_init_api_call(void) {
    conversation_t conv;
    conversation_init(&conv);
    config_t cfg;
    config_init(&cfg);
    char *session_id = nc_xstrdup("test-session");
    ASSERT(handle_slash_command("/init", &conv, &cfg, &session_id, true) == CMD_API_CALL);
    ASSERT(conv.len == 1);
    free(session_id);
    conversation_free(&conv);
    config_free(&cfg);
    return 0;
}

static int test_effort_command(void) {
    conversation_t conv;
    conversation_init(&conv);
    config_t cfg;
    config_init(&cfg);
    char *session_id = nc_xstrdup("test-session");
    effort_level_init();
    ASSERT(handle_slash_command("/effort high", &conv, &cfg, &session_id, true) == CMD_CONTINUE);
    ASSERT(effort_level_get() == EFFORT_HIGH);
    ASSERT(handle_slash_command("/effort low", &conv, &cfg, &session_id, true) == CMD_CONTINUE);
    ASSERT(effort_level_get() == EFFORT_LOW);
    free(session_id);
    conversation_free(&conv);
    config_free(&cfg);
    return 0;
}

static int test_rewind_blocked_in_dirty_repo(void) {
    char saved_cwd[NC_PATH_MAX];
    char tmpdir[] = "/tmp/nevercode-slash-rewind-XXXXXX";
    char note_path[NC_PATH_MAX];
    char contents[128];
    conversation_t conv;
    config_t cfg;
    char *session_id = nc_xstrdup("test-session");

    ASSERT_NOT_NULL(mkdtemp(tmpdir));
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(th_init_temp_repo(tmpdir), 0);
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    snprintf(note_path, sizeof(note_path), "%s/note.txt", tmpdir);
    file_history_init();
    file_history_next_turn();
    ASSERT_INT_EQ(file_history_snapshot(note_path), 0);
    ASSERT_INT_EQ(th_write_text(note_path, "two\n"), 0);

    conversation_init(&conv);
    config_init(&cfg);
    ASSERT(handle_slash_command("/rewind 1", &conv, &cfg, &session_id, true) == CMD_CONTINUE);
    ASSERT_INT_EQ(th_read_text(note_path, contents, sizeof(contents)), 0);
    ASSERT_STR_EQ(contents, "two\n");

    free(session_id);
    conversation_free(&conv);
    config_free(&cfg);
    file_history_free();
    ASSERT_INT_EQ(chdir(saved_cwd), 0);
    ASSERT_INT_EQ(th_remove_tree(tmpdir), 0);
    return 0;
}

int test_slash_commands_all(void) {
    TEST_SUITE("slash_commands");
    TEST_COUNTERS;

    RUN_TEST(test_help_command);
    RUN_TEST(test_version_command);
    RUN_TEST(test_exit_command);
    RUN_TEST(test_clear_command);
    RUN_TEST(test_plan_mode);
    RUN_TEST(test_brief_mode);
    RUN_TEST(test_model_command);
    RUN_TEST(test_unknown_command);
    RUN_TEST(test_init_api_call);
    RUN_TEST(test_effort_command);
    RUN_TEST(test_rewind_blocked_in_dirty_repo);

    return _t_failed;
}
