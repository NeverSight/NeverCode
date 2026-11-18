#include "test.h"
#include "nevercode.h"
#include "config.h"
#include "conversation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int history_save(const conversation_t *c, const char *id);
extern int history_load(conversation_t *c, const char *id);
extern int history_rename(const char *session_id, const char *name);
extern char *history_find_latest(void);
extern char *history_gen_session_id(void);
extern int nc_session_history_list(char **out);

static int test_rename_session(void) {
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "hello");

    char *sid = history_gen_session_id();
    ASSERT_INT_EQ(history_save(&conv, sid), 0);
    ASSERT_INT_EQ(history_rename(sid, "my test session"), 0);

    char *listing = NULL;
    nc_session_history_list(&listing);
    ASSERT(strstr(listing, "my test session") != NULL);
    free(listing);

    const char *home = getenv(ENV_HOME);
    if (home) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/.nevercode/history/%s.json", home, sid);
        unlink(path);
    }
    free(sid);
    conversation_free(&conv);
    return 0;
}

static int test_rename_nonexistent(void) {
    ASSERT(history_rename("nonexistent_session_XYZ_999", "test") != 0);
    return 0;
}

static int test_continue_flag(void) {
    config_t cfg;
    config_init(&cfg);
    ASSERT(cfg.continue_last == false);
    config_free(&cfg);
    return 0;
}

static int test_find_latest(void) {
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "latest session test");

    char *sid = history_gen_session_id();
    history_save(&conv, sid);

    char *latest = history_find_latest();
    ASSERT_NOT_NULL(latest);
    free(latest);

    const char *home = getenv(ENV_HOME);
    if (home) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/.nevercode/history/%s.json", home, sid);
        unlink(path);
    }
    free(sid);
    conversation_free(&conv);
    return 0;
}

static int test_history_list_with_name(void) {
    conversation_t conv;
    conversation_init(&conv);
    conversation_add_user_text(&conv, "named session");

    char *sid = history_gen_session_id();
    history_save(&conv, sid);
    history_rename(sid, "Feature Branch");

    char *listing = NULL;
    int n = nc_session_history_list(&listing);
    ASSERT(n > 0);
    ASSERT(strstr(listing, "[Feature Branch]") != NULL);
    free(listing);

    const char *home = getenv(ENV_HOME);
    if (home) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/.nevercode/history/%s.json", home, sid);
        unlink(path);
    }
    free(sid);
    conversation_free(&conv);
    return 0;
}

static int test_tab_completion_new_cmds(void) {
    extern const char **term_get_slash_cmds(void);
    const char **cmds = term_get_slash_cmds();
    int found_rename = 0, found_rewind = 0;
    for (int i = 0; cmds[i]; i++) {
        if (strcmp(cmds[i], "/rename") == 0)
            found_rename = 1;
        if (strcmp(cmds[i], "/rewind") == 0)
            found_rewind = 1;
    }
    ASSERT(found_rename);
    ASSERT(found_rewind);
    return 0;
}

int test_session_enhance_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("session_enhance");
    RUN_TEST(test_rename_session);
    RUN_TEST(test_rename_nonexistent);
    RUN_TEST(test_continue_flag);
    RUN_TEST(test_find_latest);
    RUN_TEST(test_history_list_with_name);
    RUN_TEST(test_tab_completion_new_cmds);
    TEST_RETURN_FAILED();
}
