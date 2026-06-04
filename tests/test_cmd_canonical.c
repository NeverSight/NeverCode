#include "test.h"
#include "nevercode.h"

static int test_canonicalize_simple(void) {
    char *c = cmd_canonicalize("ls -la");
    ASSERT(c != NULL);
    ASSERT(strcmp(c, "ls -la") == 0);
    free(c);
    return 0;
}

static int test_canonicalize_bash_c(void) {
    char *c = cmd_canonicalize("/bin/bash -c 'echo hello'");
    ASSERT(c != NULL);
    ASSERT(strstr(c, "__nevercode_shell_script__") != NULL);
    ASSERT(strstr(c, "echo hello") != NULL);
    free(c);
    return 0;
}

static int test_canonicalize_sh_c(void) {
    char *c = cmd_canonicalize("sh -c 'ls -la'");
    ASSERT(c != NULL);
    ASSERT(strstr(c, "__nevercode_shell_script__") != NULL);
    ASSERT(strstr(c, "ls -la") != NULL);
    free(c);
    return 0;
}

static int test_canonicalize_path_invariant(void) {
    bool eq = cmd_canonical_equal("/bin/bash -c 'echo hi'", "bash -c 'echo hi'");
    ASSERT(eq);
    return 0;
}

static int test_canonicalize_no_c_flag(void) {
    char *c = cmd_canonicalize("bash script.sh");
    ASSERT(c != NULL);
    ASSERT(strcmp(c, "bash script.sh") == 0);
    free(c);
    return 0;
}

static int test_canonicalize_null(void) {
    char *c = cmd_canonicalize(NULL);
    ASSERT(c == NULL);
    c = cmd_canonicalize("");
    ASSERT(c == NULL);
    return 0;
}

static int test_approval_cache_init_fn(void) {
    approval_cache_init();
    ASSERT(approval_cache_count_entries() == 0);
    approval_cache_free();
    return 0;
}

static int test_approval_cache_add_check(void) {
    approval_cache_init();
    approval_cache_add("ls -la");
    ASSERT(approval_cache_count_entries() == 1);
    ASSERT(approval_cache_check("ls -la"));
    ASSERT(!approval_cache_check("rm -rf"));
    approval_cache_free();
    return 0;
}

static int test_approval_cache_dedup(void) {
    approval_cache_init();
    approval_cache_add("git status");
    approval_cache_add("git status");
    ASSERT(approval_cache_count_entries() == 1);
    approval_cache_free();
    return 0;
}

static int test_approval_cache_canonical(void) {
    approval_cache_init();
    approval_cache_add("/bin/bash -c 'echo test'");
    ASSERT(approval_cache_check("bash -c 'echo test'"));
    approval_cache_free();
    return 0;
}

static int test_canonicalize_zsh(void) {
    char *c = cmd_canonicalize("zsh -c 'echo hello'");
    ASSERT(c != NULL);
    ASSERT(strstr(c, "__nevercode_shell_script__") != NULL);
    free(c);
    return 0;
}

int test_cmd_canonical_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("cmd_canonical");

    RUN_TEST(test_canonicalize_simple);
    RUN_TEST(test_canonicalize_bash_c);
    RUN_TEST(test_canonicalize_sh_c);
    RUN_TEST(test_canonicalize_path_invariant);
    RUN_TEST(test_canonicalize_no_c_flag);
    RUN_TEST(test_canonicalize_null);
    RUN_TEST(test_approval_cache_init_fn);
    RUN_TEST(test_approval_cache_add_check);
    RUN_TEST(test_approval_cache_dedup);
    RUN_TEST(test_approval_cache_canonical);
    RUN_TEST(test_canonicalize_zsh);

    TEST_RETURN_FAILED();
}
