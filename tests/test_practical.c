#include "test.h"
#include "nevercode.h"
#include "conversation.h"
#include <unistd.h>
#include <sys/stat.h>

/* ---- /files tests ---- */

static int test_files_empty(void) {
    read_file_tracker_t t;
    read_file_tracker_init(&t);
    char *out = files_in_context(&t);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "No files") != NULL);
    free(out);
    read_file_tracker_free(&t);
    return 0;
}

static int test_files_with_entries(void) {
    read_file_tracker_t t;
    read_file_tracker_init(&t);
    read_file_tracker_add(&t, "/tmp/a.c");
    read_file_tracker_add(&t, "/tmp/b.c");
    char *out = files_in_context(&t);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "2") != NULL);
    ASSERT(strstr(out, "a.c") != NULL);
    ASSERT(strstr(out, "b.c") != NULL);
    free(out);
    read_file_tracker_free(&t);
    return 0;
}

/* ---- /pr prompt tests ---- */

static int test_pr_prompt(void) {
    char *p = build_pr_prompt();
    ASSERT_NOT_NULL(p);
    ASSERT(strstr(p, "pull request") != NULL);
    ASSERT(strstr(p, "gh pr create") != NULL);
    ASSERT(strstr(p, "NEVER force push") != NULL);
    ASSERT(strstr(p, "Summary") != NULL);
    free(p);
    return 0;
}

/* ---- custom skills tests ---- */

static int test_custom_skill_find_none(void) {
    ASSERT_NULL(custom_skill_find("nonexistent"));
    return 0;
}

static int test_custom_skill_load_dir(void) {
    char dir[256];
    snprintf(dir, sizeof(dir), "/tmp/nevercode_test_skills_%d", (int)getpid());
    mkdir(dir, 0755);

    char path[512];
    snprintf(path, sizeof(path), "%s/deploy.md", dir);
    FILE *f = fopen(path, "w");
    fprintf(f, "Deploy the application to production.\n"
               "1. Run tests first\n2. Build\n3. Deploy\n");
    fclose(f);

    int loaded = custom_skills_load(dir);
    ASSERT(loaded >= 1);

    const char *prompt = custom_skill_find("deploy");
    ASSERT_NOT_NULL(prompt);
    ASSERT(strstr(prompt, "Deploy") != NULL);

    char *names[16];
    int n = custom_skill_list(names, 16);
    ASSERT(n >= 1);

    unlink(path);
    rmdir(dir);
    custom_skills_free();
    return 0;
}

static int test_custom_skill_list_empty(void) {
    char *names[4];
    int n = custom_skill_list(names, 4);
    ASSERT_INT_EQ(n, 0);
    return 0;
}

/* ---- MCP management tests ---- */

static int test_mcp_list_nofile(void) {
    char *out = mcp_format_server_list("/tmp/nonexistent_mcp_cfg.json");
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "No MCP") != NULL);
    free(out);
    return 0;
}

static int test_mcp_add_remove(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/nevercode_test_mcp_%d.json", (int)getpid());

    int rc = mcp_add_server_to_config(path, "test-server", "npx test-mcp");
    ASSERT_INT_EQ(rc, 0);

    rc = mcp_add_server_to_config(path, "test-server", "npx test-mcp");
    ASSERT_INT_EQ(rc, -2);

    char *listing = mcp_format_server_list(path);
    ASSERT_NOT_NULL(listing);
    ASSERT(strstr(listing, "test-server") != NULL);
    ASSERT(strstr(listing, "npx test-mcp") != NULL);
    free(listing);

    rc = mcp_remove_server_from_config(path, "test-server");
    ASSERT_INT_EQ(rc, 0);

    rc = mcp_remove_server_from_config(path, "test-server");
    ASSERT_INT_EQ(rc, -1);

    unlink(path);
    return 0;
}

static int test_mcp_add_null(void) {
    int rc = mcp_add_server_to_config("/dev/null", NULL, "cmd");
    ASSERT(rc < 0);
    rc = mcp_add_server_to_config("/dev/null", "name", NULL);
    ASSERT(rc < 0);
    return 0;
}

/* ---- Tab completion ---- */

extern const char **term_get_slash_cmds(void);

static int test_tab_has_new_cmds(void) {
    const char **cmds = term_get_slash_cmds();
    ASSERT_NOT_NULL(cmds);
    bool has_files = false, has_pr = false, has_mcp = false;
    for (int i = 0; cmds[i]; i++) {
        if (strcmp(cmds[i], "/files") == 0)
            has_files = true;
        if (strcmp(cmds[i], "/pr") == 0)
            has_pr = true;
        if (strcmp(cmds[i], "/mcp") == 0)
            has_mcp = true;
    }
    ASSERT(has_files);
    ASSERT(has_pr);
    ASSERT(has_mcp);
    return 0;
}

int test_practical_all(void) {
    TEST_SUITE("practical");
    TEST_COUNTERS;

    RUN_TEST(test_files_empty);
    RUN_TEST(test_files_with_entries);
    RUN_TEST(test_pr_prompt);
    RUN_TEST(test_custom_skill_find_none);
    RUN_TEST(test_custom_skill_load_dir);
    RUN_TEST(test_custom_skill_list_empty);
    RUN_TEST(test_mcp_list_nofile);
    RUN_TEST(test_mcp_add_remove);
    RUN_TEST(test_mcp_add_null);
    RUN_TEST(test_tab_has_new_cmds);

    TEST_RETURN_FAILED();
}
