#include "test.h"
#include "nevercode.h"

static int test_detect_zsh(void) {
    ASSERT(shell_detect_from_path("/bin/zsh") == SHELL_ZSH);
    ASSERT(shell_detect_from_path("/usr/bin/zsh") == SHELL_ZSH);
    return 0;
}

static int test_detect_bash(void) {
    ASSERT(shell_detect_from_path("/bin/bash") == SHELL_BASH);
    ASSERT(shell_detect_from_path("/usr/local/bin/bash") == SHELL_BASH);
    return 0;
}

static int test_detect_sh(void) {
    ASSERT(shell_detect_from_path("/bin/sh") == SHELL_SH);
    return 0;
}

static int test_detect_fish(void) {
    ASSERT(shell_detect_from_path("/usr/bin/fish") == SHELL_FISH);
    return 0;
}

static int test_detect_unknown(void) {
    ASSERT(shell_detect_from_path("/usr/bin/something") == SHELL_UNKNOWN);
    ASSERT(shell_detect_from_path(NULL) == SHELL_UNKNOWN);
    return 0;
}

static int test_type_name(void) {
    ASSERT(strcmp(shell_type_name(SHELL_ZSH), "zsh") == 0);
    ASSERT(strcmp(shell_type_name(SHELL_BASH), "bash") == 0);
    ASSERT(strcmp(shell_type_name(SHELL_FISH), "fish") == 0);
    ASSERT(strcmp(shell_type_name(SHELL_UNKNOWN), "unknown") == 0);
    return 0;
}

static int test_init_file(void) {
    const char *zshrc = shell_init_file(SHELL_ZSH);
    ASSERT(strstr(zshrc, "zshrc") != NULL);
    const char *bashrc = shell_init_file(SHELL_BASH);
    ASSERT(strstr(bashrc, "bashrc") != NULL);
    const char *fishrc = shell_init_file(SHELL_FISH);
    ASSERT(strstr(fishrc, "fish") != NULL);
    return 0;
}

static int test_prompt_var(void) {
    ASSERT(strcmp(shell_prompt_var(SHELL_ZSH), "PS1") == 0);
    ASSERT(strcmp(shell_prompt_var(SHELL_BASH), "PS1") == 0);
    ASSERT(strcmp(shell_prompt_var(SHELL_FISH), "fish_prompt") == 0);
    return 0;
}

static int test_supports_source(void) {
    ASSERT(shell_supports_source(SHELL_ZSH));
    ASSERT(shell_supports_source(SHELL_BASH));
    ASSERT(!shell_supports_source(SHELL_CSH));
    ASSERT(!shell_supports_source(SHELL_TCSH));
    return 0;
}

static int test_shell_detect_describe_fn(void) {
    shell_detect_init();
    char *desc = shell_detect_describe();
    ASSERT(desc != NULL);
    ASSERT(strstr(desc, "Shell:") != NULL);
    ASSERT(strstr(desc, "Init file:") != NULL);
    free(desc);
    return 0;
}

int test_shell_detect_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("shell_detect");

    RUN_TEST(test_detect_zsh);
    RUN_TEST(test_detect_bash);
    RUN_TEST(test_detect_sh);
    RUN_TEST(test_detect_fish);
    RUN_TEST(test_detect_unknown);
    RUN_TEST(test_type_name);
    RUN_TEST(test_init_file);
    RUN_TEST(test_prompt_var);
    RUN_TEST(test_supports_source);
    RUN_TEST(test_shell_detect_describe_fn);

    TEST_RETURN_FAILED();
}
