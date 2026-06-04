#include "test.h"
#include "nevercode.h"
#include "tools.h"
#include <unistd.h>
#include <sys/stat.h>

extern char *build_system_prompt(void);

static int test_prompt_includes_environment(void) {
    char *prompt = build_system_prompt();
    ASSERT_NOT_NULL(prompt);
    ASSERT(strstr(prompt, "Working directory:") != NULL);
    ASSERT(strstr(prompt, "OS:") != NULL);
    ASSERT(strstr(prompt, "Date:") != NULL);
    free(prompt);
    return 0;
}

static int test_prompt_includes_tools_section(void) {
    char *prompt = build_system_prompt();
    ASSERT_NOT_NULL(prompt);
    ASSERT(strstr(prompt, "Available Tools") != NULL);
    free(prompt);
    return 0;
}

static int test_prompt_includes_ctf_section(void) {
    char *prompt = build_system_prompt();
    ASSERT_NOT_NULL(prompt);
    ASSERT(strstr(prompt, "CTF") != NULL);
    ASSERT(strstr(prompt, "no restrictions") != NULL);
    free(prompt);
    return 0;
}

static int test_prompt_no_telemetry(void) {
    char *prompt = build_system_prompt();
    ASSERT_NOT_NULL(prompt);
    ASSERT(strstr(prompt, "telemetry") == NULL);
    ASSERT(strstr(prompt, "analytics") == NULL);
    ASSERT(strstr(prompt, "sentry") == NULL);
    free(prompt);
    return 0;
}

static int test_prompt_full_access(void) {
    char *prompt = build_system_prompt();
    ASSERT_NOT_NULL(prompt);
    ASSERT(strstr(prompt, "full access") != NULL || strstr(prompt, "full permissions") != NULL ||
           strstr(prompt, "no restrictions") != NULL);
    free(prompt);
    return 0;
}

extern char *agents_md_load(void);

static int test_prompt_project_doc_loading(void) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/nevercode_test_doc_%d", getpid());
    mkdir(tmpdir, 0755);

    char agents_path[512];
    snprintf(agents_path, sizeof(agents_path), "%s/AGENTS.md", tmpdir);
    FILE *f = fopen(agents_path, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "# Test AGENTS.md\nBuild: make test\n");
    fclose(f);

    char saved_cwd[4096];
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(chdir(tmpdir), 0);
    project_root_reset();

    char *loaded = agents_md_load();
    ASSERT_NOT_NULL(loaded);
    ASSERT(strstr(loaded, "AGENTS.md") != NULL);
    ASSERT(strstr(loaded, "Build: make test") != NULL);

    chdir(saved_cwd);
    project_root_reset();
    unlink(agents_path);
    rmdir(tmpdir);
    free(loaded);
    return 0;
}

static int test_prompt_compat_md_loading(void) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/nevercode_test_compat_%d", getpid());
    mkdir(tmpdir, 0755);

    char claude_path[512];
    snprintf(claude_path, sizeof(claude_path), "%s/CLAUDE.md", tmpdir);
    FILE *f = fopen(claude_path, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "# CLAUDE.md Test\nCustom rules here\n");
    fclose(f);

    char saved_cwd[4096];
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(chdir(tmpdir), 0);
    project_root_reset();

    char *loaded = agents_md_load();
    ASSERT_NOT_NULL(loaded);
    ASSERT(strstr(loaded, "CLAUDE.md") != NULL);
    ASSERT(strstr(loaded, "Custom rules here") != NULL);

    chdir(saved_cwd);
    project_root_reset();
    unlink(claude_path);
    rmdir(tmpdir);
    free(loaded);
    return 0;
}

static int test_prompt_conciseness_guidance(void) {
    char *prompt = build_system_prompt();
    ASSERT_NOT_NULL(prompt);
    ASSERT(strstr(prompt, "concise") != NULL);
    ASSERT(strstr(prompt, "preamble") != NULL || strstr(prompt, "postamble") != NULL);
    free(prompt);
    return 0;
}

static int test_prompt_code_reference_hint(void) {
    char *prompt = build_system_prompt();
    ASSERT_NOT_NULL(prompt);
    ASSERT(strstr(prompt, "file_path:line_number") != NULL);
    free(prompt);
    return 0;
}

static int test_project_doc_paths_table(void) {
    ASSERT_NOT_NULL(nc_project_doc_leaf_names);
    ASSERT_NOT_NULL(nc_project_doc_agents_paths);
    ASSERT_NOT_NULL(nc_project_doc_extra_paths);
    ASSERT(nc_project_doc_leaf_names[0] && nc_project_doc_leaf_names[0][0] != '\0');
    ASSERT(nc_project_doc_agents_paths[0] && nc_project_doc_agents_paths[0][0] != '\0');
    ASSERT(nc_project_doc_extra_paths[0] && nc_project_doc_extra_paths[0][0] != '\0');
    ASSERT_INT_EQ(nc_project_doc_leaf_n, 12);
    ASSERT_INT_EQ(nc_project_doc_agents_n, 24);
    ASSERT_INT_EQ(nc_project_doc_extra_n, 24);
    ASSERT_TRUE(nc_project_doc_is_filename("AGENTS.local.md"));
    ASSERT_TRUE(nc_project_doc_is_filename("claude.override.md"));
    ASSERT_FALSE(nc_project_doc_is_filename("hooks.json"));
    return 0;
}

static int test_same_dir_agents_and_extra_merged(void) {
    char tmpdir[256];
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/nevercode_test_dual_doc_%d", getpid());
    mkdir(tmpdir, 0755);

    char p1[512], p2[512];
    snprintf(p1, sizeof(p1), "%s/AGENTS.md", tmpdir);
    snprintf(p2, sizeof(p2), "%s/claude.md", tmpdir);
    FILE *f = fopen(p1, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "from-agents-family\n");
    fclose(f);
    f = fopen(p2, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "from-extra-family\n");
    fclose(f);

    char saved_cwd[4096];
    ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));
    ASSERT_INT_EQ(chdir(tmpdir), 0);
    project_root_reset();

    char *loaded = agents_md_load();
    ASSERT_NOT_NULL(loaded);
    ASSERT(strstr(loaded, "from-agents-family") != NULL);
    ASSERT(strstr(loaded, "from-extra-family") != NULL);

    chdir(saved_cwd);
    project_root_reset();
    unlink(p1);
    unlink(p2);
    rmdir(tmpdir);
    free(loaded);
    return 0;
}

int test_project_doc_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("project_doc");
    RUN_TEST(test_prompt_includes_environment);
    RUN_TEST(test_prompt_includes_tools_section);
    RUN_TEST(test_prompt_includes_ctf_section);
    RUN_TEST(test_prompt_no_telemetry);
    RUN_TEST(test_prompt_full_access);
    RUN_TEST(test_prompt_conciseness_guidance);
    RUN_TEST(test_prompt_code_reference_hint);
    RUN_TEST(test_prompt_project_doc_loading);
    RUN_TEST(test_prompt_compat_md_loading);
    RUN_TEST(test_same_dir_agents_and_extra_merged);
    RUN_TEST(test_project_doc_paths_table);
    TEST_RETURN_FAILED();
}
