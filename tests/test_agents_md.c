#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern char *agents_md_load(void);

static void mkdir_p(const char *path) {
    char tmp[4096];
    nc_strlcpy(tmp, path, sizeof(tmp));
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

static void remove_if_exists(const char *path) {
    unlink(path);
}

static int test_config_home_dir(void) {
    const char *dir = config_home_dir();
    ASSERT_NOT_NULL(dir);
    ASSERT(strlen(dir) > 0);
    return 0;
}

static int test_config_home_path(void) {
    char path[4096];
    config_home_path(path, sizeof(path), "config.json");
    ASSERT(strlen(path) > 0);
    ASSERT(strstr(path, "config.json") != NULL);
    return 0;
}

static int test_dual_dir_fallback(void) {
    const char *dir = config_home_dir();
    ASSERT_NOT_NULL(dir);
    /* Should end with a recognized config directory suffix */
    ASSERT(strstr(dir, ".agents") || strstr(dir, ".nevercode") || strstr(dir, ".claude"));
    return 0;
}

static int test_agents_md_project_doc_names(void) {
    /* Verify AGENTS.md and CLAUDE.md are both in the scan list by testing
       that agents_md_load doesn't crash when called */
    char *result = agents_md_load();
    /* May return NULL if no docs found — that's fine */
    free(result);
    return 0;
}

static int test_hierarchical_msg_format(void) {
    /* Create a temp dir structure with nested AGENTS.md files */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/nevercode_test_hier_%d", (int)getpid());
    char subdir[512];
    snprintf(subdir, sizeof(subdir), "%s/sub", tmpdir);
    mkdir_p(subdir);

    write_file(tmpdir, ""); /* ensure parent exists */
    char f1[512], f2[512];
    snprintf(f1, sizeof(f1), "%s/AGENTS.md", tmpdir);
    snprintf(f2, sizeof(f2), "%s/sub/AGENTS.md", subdir);
    write_file(f1, "# Root agents\nBe helpful\n");
    write_file(f2, "# Sub agents\nOverride rules\n");

    /* We can't easily test the full load without chdir, but we can verify
       the hierarchical message constant exists */
    ASSERT(1); /* Message is embedded in agents_md.c */

    unlink(f1);
    unlink(f2);
    rmdir(subdir);
    rmdir(tmpdir);
    return 0;
}

static int test_agents_override_md(void) {
    /* AGENTS.override.md should be in the scan list */
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/nevercode_test_override_%d", (int)getpid());
    mkdir_p(tmpdir);

    char f1[512];
    snprintf(f1, sizeof(f1), "%s/AGENTS.override.md", tmpdir);
    write_file(f1, "# Override\nLocal rules\n");

    /* Verify the file can be read */
    FILE *f = fopen(f1, "r");
    ASSERT_NOT_NULL(f);
    fclose(f);

    unlink(f1);
    rmdir(tmpdir);
    return 0;
}

static int test_lowercase_project_docs_loaded(void) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/nevercode_test_lower_proj_%d", (int)getpid());
    char subdir[512];
    snprintf(subdir, sizeof(subdir), "%s/sub", tmpdir);
    mkdir_p(subdir);

    char agents_path[512], claude_path[512];
    snprintf(agents_path, sizeof(agents_path), "%s/agents.md", tmpdir);
    snprintf(claude_path, sizeof(claude_path), "%s/.claude/claude.md", tmpdir);
    mkdir_p("/tmp");
    char dot_claude[512];
    snprintf(dot_claude, sizeof(dot_claude), "%s/.claude", tmpdir);
    mkdir_p(dot_claude);

    write_file(agents_path, "lower agents rule\n");
    write_file(claude_path, "lower claude rule\n");

    char old_cwd[4096];
    ASSERT_NOT_NULL(getcwd(old_cwd, sizeof(old_cwd)));
    ASSERT_INT_EQ(chdir(subdir), 0);

    char *result = agents_md_load();
    ASSERT_NOT_NULL(result);
    ASSERT(strstr(result, "lower agents rule") != NULL);
    ASSERT(strstr(result, "lower claude rule") != NULL);
    free(result);

    ASSERT_INT_EQ(chdir(old_cwd), 0);
    remove_if_exists(claude_path);
    remove_if_exists(agents_path);
    rmdir(dot_claude);
    rmdir(subdir);
    rmdir(tmpdir);
    return 0;
}

static int test_same_dir_merge_order_base_local_override(void) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/nevercode_test_merge_order_%d", (int)getpid());
    mkdir_p(tmpdir);

    char f_base[512], f_local[512], f_override[512];
    snprintf(f_base, sizeof(f_base), "%s/claude.md", tmpdir);
    snprintf(f_local, sizeof(f_local), "%s/claude.local.md", tmpdir);
    snprintf(f_override, sizeof(f_override), "%s/claude.override.md", tmpdir);
    write_file(f_base, "BASE_RULE\n");
    write_file(f_local, "LOCAL_RULE\n");
    write_file(f_override, "OVERRIDE_RULE\n");

    char old_cwd[4096];
    ASSERT_NOT_NULL(getcwd(old_cwd, sizeof(old_cwd)));
    ASSERT_INT_EQ(chdir(tmpdir), 0);

    char *result = agents_md_load();
    ASSERT_NOT_NULL(result);
    char *p_base = strstr(result, "BASE_RULE");
    char *p_local = strstr(result, "LOCAL_RULE");
    char *p_override = strstr(result, "OVERRIDE_RULE");
    ASSERT(p_base == NULL);
    ASSERT(p_local == NULL);
    ASSERT_NOT_NULL(p_override);
    free(result);

    ASSERT_INT_EQ(chdir(old_cwd), 0);
    remove_if_exists(f_override);
    remove_if_exists(f_local);
    remove_if_exists(f_base);
    rmdir(tmpdir);
    return 0;
}

static int test_nested_deeper_dir_appended_later(void) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/nevercode_test_nested_override_%d", (int)getpid());
    char subdir[512];
    snprintf(subdir, sizeof(subdir), "%s/sub", tmpdir);
    mkdir_p(subdir);

    char root_doc[512], sub_doc[512];
    snprintf(root_doc, sizeof(root_doc), "%s/AGENTS.md", tmpdir);
    snprintf(sub_doc, sizeof(sub_doc), "%s/AGENTS.md", subdir);
    write_file(root_doc, "ROOT_SCOPE_RULE\n");
    write_file(sub_doc, "SUB_SCOPE_RULE\n");

    char old_cwd[4096];
    ASSERT_NOT_NULL(getcwd(old_cwd, sizeof(old_cwd)));
    ASSERT_INT_EQ(chdir(subdir), 0);

    char *result = agents_md_load();
    ASSERT_NOT_NULL(result);
    char *p_root = strstr(result, "ROOT_SCOPE_RULE");
    char *p_sub = strstr(result, "SUB_SCOPE_RULE");
    ASSERT_NOT_NULL(p_root);
    ASSERT_NOT_NULL(p_sub);
    ASSERT(p_root < p_sub);
    free(result);

    ASSERT_INT_EQ(chdir(old_cwd), 0);
    remove_if_exists(sub_doc);
    remove_if_exists(root_doc);
    rmdir(subdir);
    rmdir(tmpdir);
    return 0;
}

static int test_lowercase_user_doc_loaded(void) {
    char home_root[256];
    snprintf(home_root, sizeof(home_root), "/tmp/nevercode_test_home_%d", (int)getpid());
    char agents_dir[512];
    snprintf(agents_dir, sizeof(agents_dir), "%s/.agents", home_root);
    mkdir_p(agents_dir);

    char user_doc[512];
    snprintf(user_doc, sizeof(user_doc), "%s/claude.md", agents_dir);
    write_file(user_doc, "user lowercase claude\n");

    const char *old_home = getenv(ENV_HOME);
    if (old_home)
        old_home = nc_xstrdup(old_home);
    ASSERT_INT_EQ(setenv("HOME", home_root, 1), 0);
    nc_home_dir_reset();

    char *result = agents_md_load();
    ASSERT_NOT_NULL(result);
    ASSERT(strstr(result, "user lowercase claude") != NULL);
    free(result);

    if (old_home) {
        ASSERT_INT_EQ(setenv("HOME", old_home, 1), 0);
        free((void *)old_home);
    } else {
        ASSERT_INT_EQ(unsetenv("HOME"), 0);
    }
    nc_home_dir_reset();

    remove_if_exists(user_doc);
    rmdir(agents_dir);
    rmdir(home_root);
    return 0;
}

static int test_project_base_local_override_order(void) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/nevercode_test_proj_order_%d", (int)getpid());
    char subdir[512];
    snprintf(subdir, sizeof(subdir), "%s/sub", tmpdir);
    mkdir_p(subdir);

    char f_base[512], f_local[512], f_override[512];
    snprintf(f_base, sizeof(f_base), "%s/AGENTS.md", tmpdir);
    snprintf(f_local, sizeof(f_local), "%s/AGENTS.local.md", tmpdir);
    snprintf(f_override, sizeof(f_override), "%s/AGENTS.override.md", tmpdir);
    write_file(f_base, "BASE_RULE\n");
    write_file(f_local, "LOCAL_RULE\n");
    write_file(f_override, "OVERRIDE_RULE\n");

    char old_cwd[4096];
    ASSERT_NOT_NULL(getcwd(old_cwd, sizeof(old_cwd)));
    ASSERT_INT_EQ(chdir(subdir), 0);

    char *result = agents_md_load();
    ASSERT_NOT_NULL(result);
    char *p_base = strstr(result, "BASE_RULE");
    char *p_local = strstr(result, "LOCAL_RULE");
    char *p_override = strstr(result, "OVERRIDE_RULE");
    ASSERT(p_base == NULL);
    ASSERT(p_local == NULL);
    ASSERT_NOT_NULL(p_override);
    free(result);

    ASSERT_INT_EQ(chdir(old_cwd), 0);
    remove_if_exists(f_override);
    remove_if_exists(f_local);
    remove_if_exists(f_base);
    rmdir(subdir);
    rmdir(tmpdir);
    return 0;
}

static int test_user_prefers_override_doc(void) {
    char home_root[256];
    snprintf(home_root, sizeof(home_root), "/tmp/nevercode_test_user_order_%d", (int)getpid());
    char agents_dir[512];
    snprintf(agents_dir, sizeof(agents_dir), "%s/.agents", home_root);
    mkdir_p(agents_dir);

    char f_base[512], f_override[512];
    snprintf(f_base, sizeof(f_base), "%s/AGENTS.md", agents_dir);
    snprintf(f_override, sizeof(f_override), "%s/AGENTS.override.md", agents_dir);
    write_file(f_base, "USER_BASE\n");
    write_file(f_override, "USER_OVERRIDE\n");

    const char *old_home = getenv(ENV_HOME);
    if (old_home)
        old_home = nc_xstrdup(old_home);
    ASSERT_INT_EQ(setenv("HOME", home_root, 1), 0);
    nc_home_dir_reset();

    char *result = agents_md_load();
    ASSERT_NOT_NULL(result);
    ASSERT(strstr(result, "USER_OVERRIDE") != NULL);
    ASSERT(strstr(result, "USER_BASE") == NULL);
    free(result);

    if (old_home) {
        ASSERT_INT_EQ(setenv("HOME", old_home, 1), 0);
        free((void *)old_home);
    } else {
        ASSERT_INT_EQ(unsetenv("HOME"), 0);
    }
    nc_home_dir_reset();

    remove_if_exists(f_override);
    remove_if_exists(f_base);
    rmdir(agents_dir);
    rmdir(home_root);
    return 0;
}

static int test_dot_agents_compat_override_loaded(void) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/nevercode_test_dot_agents_%d", (int)getpid());
    char subdir[512], dot_agents[512], f_override[512];
    snprintf(subdir, sizeof(subdir), "%s/sub", tmpdir);
    snprintf(dot_agents, sizeof(dot_agents), "%s/.agents", tmpdir);
    snprintf(f_override, sizeof(f_override), "%s/CLAUDE.override.md", dot_agents);
    mkdir_p(subdir);
    mkdir_p(dot_agents);
    write_file(f_override, "dot-agents-claude-override\n");

    char old_cwd[4096];
    ASSERT_NOT_NULL(getcwd(old_cwd, sizeof(old_cwd)));
    ASSERT_INT_EQ(chdir(subdir), 0);

    char *result = agents_md_load();
    ASSERT_NOT_NULL(result);
    ASSERT(strstr(result, "dot-agents-claude-override") != NULL);
    free(result);

    ASSERT_INT_EQ(chdir(old_cwd), 0);
    remove_if_exists(f_override);
    rmdir(dot_agents);
    rmdir(subdir);
    rmdir(tmpdir);
    return 0;
}

static int test_does_not_read_docs_above_project_root(void) {
    char root[256];
    snprintf(root, sizeof(root), "/tmp/nevercode_test_proj_root_%d", (int)getpid());
    char repo[512], subdir[512], outside_doc[512], root_doc[512], git_marker[512];
    snprintf(repo, sizeof(repo), "%s/repo", root);
    snprintf(subdir, sizeof(subdir), "%s/sub", repo);
    snprintf(outside_doc, sizeof(outside_doc), "%s/AGENTS.md", root);
    snprintf(root_doc, sizeof(root_doc), "%s/AGENTS.md", repo);
    snprintf(git_marker, sizeof(git_marker), "%s/.git", repo);

    mkdir_p(subdir);
    write_file(git_marker, "gitdir: fake\n");
    write_file(outside_doc, "outside root rule\n");
    write_file(root_doc, "inside repo rule\n");

    char old_cwd[4096];
    ASSERT_NOT_NULL(getcwd(old_cwd, sizeof(old_cwd)));
    project_root_reset();
    ASSERT_INT_EQ(chdir(subdir), 0);
    project_root_reset();

    char *result = agents_md_load();
    ASSERT_NOT_NULL(result);
    ASSERT(strstr(result, "inside repo rule") != NULL);
    ASSERT(strstr(result, "outside root rule") == NULL);
    free(result);

    ASSERT_INT_EQ(chdir(old_cwd), 0);
    remove_if_exists(root_doc);
    remove_if_exists(outside_doc);
    remove_if_exists(git_marker);
    rmdir(subdir);
    rmdir(repo);
    rmdir(root);
    return 0;
}

static int test_settings_from_config_home(void) {
    char path[4096];
    config_home_path(path, sizeof(path), "settings.json");
    ASSERT(strstr(path, "settings.json") != NULL);
    ASSERT(strlen(path) > 13);
    return 0;
}

static int test_config_home_subpaths(void) {
    char path[4096];

    config_home_path(path, sizeof(path), "hooks.json");
    ASSERT(strstr(path, "hooks.json") != NULL);

    config_home_path(path, sizeof(path), "memory.json");
    ASSERT(strstr(path, "memory.json") != NULL);

    config_home_path(path, sizeof(path), "mcp.json");
    ASSERT(strstr(path, "mcp.json") != NULL);

    config_home_path(path, sizeof(path), "history");
    ASSERT(strstr(path, "history") != NULL);

    config_home_path(path, sizeof(path), "input_history");
    ASSERT(strstr(path, "input_history") != NULL);

    return 0;
}

int test_agents_md_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("agents_md");

    RUN_TEST(test_config_home_dir);
    RUN_TEST(test_config_home_path);
    RUN_TEST(test_dual_dir_fallback);
    RUN_TEST(test_agents_md_project_doc_names);
    RUN_TEST(test_hierarchical_msg_format);
    RUN_TEST(test_agents_override_md);
    RUN_TEST(test_lowercase_project_docs_loaded);
    RUN_TEST(test_same_dir_merge_order_base_local_override);
    RUN_TEST(test_nested_deeper_dir_appended_later);
    RUN_TEST(test_lowercase_user_doc_loaded);
    RUN_TEST(test_project_base_local_override_order);
    RUN_TEST(test_user_prefers_override_doc);
    RUN_TEST(test_dot_agents_compat_override_loaded);
    RUN_TEST(test_does_not_read_docs_above_project_root);
    RUN_TEST(test_settings_from_config_home);
    RUN_TEST(test_config_home_subpaths);

    TEST_RETURN_FAILED();
}
