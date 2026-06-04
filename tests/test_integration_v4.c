#include "test.h"
#include "nevercode.h"
#include "permission.h"
#include "terminal.h"
#include <stdlib.h>
#include <string.h>

static int test_approval_cache_bash_integration(void) {
    approval_cache_init();
    approval_cache_add("git status");
    ASSERT(approval_cache_check("git status"));
    ASSERT(!approval_cache_check("git push --force"));

    approval_cache_add("/bin/bash -c 'echo hi'");
    ASSERT(approval_cache_check("bash -c 'echo hi'"));
    approval_cache_free();
    return 0;
}

static int test_shell_detect_after_init(void) {
    shell_detect_init();
    shell_type_t st = shell_detected();
    ASSERT(st != SHELL_UNKNOWN);
    const char *path = shell_detected_path();
    ASSERT(path != NULL);
    ASSERT(strlen(path) > 0);
    return 0;
}

static int test_shell_detect_in_env(void) {
    shell_detect_init();
    char *desc = shell_detect_describe();
    ASSERT(desc != NULL);
    ASSERT(strstr(desc, "Shell:") != NULL);
    ASSERT(strstr(desc, "Init file:") != NULL);
    ASSERT(strstr(desc, "Prompt var:") != NULL);
    free(desc);
    return 0;
}

static int test_commit_attr_integration(void) {
    commit_attr_init(NULL);
    char *inst = commit_attr_instruction();
    ASSERT(inst != NULL);
    ASSERT(strstr(inst, "Co-authored-by") != NULL);
    ASSERT(strstr(inst, NC_PRODUCT_NAME) != NULL);
    free(inst);

    char *out = commit_msg_ensure_trailer("fix: update deps\n");
    ASSERT(out != NULL);
    ASSERT(strstr(out, "Co-authored-by") != NULL);
    ASSERT(strstr(out, "fix: update deps") != NULL);
    free(out);
    return 0;
}

static int test_text_encoding_file_read_guard(void) {
    ASSERT(text_is_binary_file("image.png"));
    ASSERT(text_is_binary_file("program.exe"));
    ASSERT(text_is_binary_file("font.woff2"));
    ASSERT(!text_is_binary_file("code.c"));
    ASSERT(!text_is_binary_file("script.py"));

    const unsigned char ok[] = "int main() { return 0; }\n";
    ASSERT(!text_is_binary_data(ok, sizeof(ok) - 1));

    const unsigned char bad[] = {0x7f, 0x45, 0x4c, 0x46, 0x00, 0x00, 0x00};
    ASSERT(text_is_binary_data(bad, sizeof(bad)));
    return 0;
}

static int test_cmd_canonical_approval_flow(void) {
    approval_cache_init();

    approval_cache_add("bash -c 'npm test'");
    ASSERT(approval_cache_check("/bin/bash -c 'npm test'"));

    approval_cache_add("git diff --stat");
    ASSERT(approval_cache_check("git diff --stat"));
    ASSERT(!approval_cache_check("git diff --staged"));

    ASSERT(approval_cache_count_entries() == 2);
    approval_cache_free();
    return 0;
}

static int test_exec_rules_persist_in_memory(void) {
    exec_rules_ctx_t r;
    exec_rules_init(&r);
    exec_rules_add(&r, "python3", EXEC_ALLOW, "trusted");
    exec_rules_add(&r, "rm -rf", EXEC_DENY, "dangerous");

    const char *just = NULL;
    ASSERT(exec_rules_check(&r, "python3 test.py", &just) == EXEC_ALLOW);
    ASSERT(exec_rules_check(&r, "rm -rf /tmp/x", &just) == EXEC_DENY);

    char *fmt = exec_rules_format(&r);
    ASSERT(fmt != NULL);
    ASSERT(strstr(fmt, "python3") != NULL);
    ASSERT(strstr(fmt, "rm -rf") != NULL);
    free(fmt);

    exec_rules_free(&r);
    return 0;
}

static int test_shell_type_all_variants(void) {
    ASSERT(shell_detect_from_path("/usr/bin/dash") == SHELL_DASH);
    ASSERT(shell_detect_from_path("/usr/bin/ksh") == SHELL_KSH);
    ASSERT(shell_detect_from_path("/usr/bin/tcsh") == SHELL_TCSH);
    ASSERT(shell_detect_from_path("/usr/bin/csh") == SHELL_CSH);
    ASSERT(strcmp(shell_type_name(SHELL_DASH), "dash") == 0);
    ASSERT(strcmp(shell_type_name(SHELL_KSH), "ksh") == 0);
    return 0;
}

static int test_text_encoding_utf8_validation(void) {
    const unsigned char valid[] = "Hello \xc3\xa9\xc3\xa0\xc3\xbc";
    ASSERT(!text_is_binary_data(valid, sizeof(valid) - 1));
    ASSERT(strcmp(text_encoding_name(valid, sizeof(valid) - 1), "utf-8") == 0);
    size_t prefix = text_valid_utf8_prefix(valid, sizeof(valid) - 1);
    ASSERT(prefix == sizeof(valid) - 1);

    char *sanitized = text_sanitize_for_display(valid, sizeof(valid) - 1);
    ASSERT(sanitized != NULL);
    ASSERT(strcmp(sanitized, (const char *)valid) == 0);
    free(sanitized);
    return 0;
}

static int test_combined_systems(void) {
    shell_detect_init();
    approval_cache_init();
    commit_attr_init(NULL);

    ASSERT(shell_detected() != SHELL_UNKNOWN);
    ASSERT(approval_cache_count_entries() == 0);
    ASSERT(commit_attr_value() != NULL);

    approval_cache_add("make test");
    ASSERT(approval_cache_check("make test"));

    approval_cache_free();
    return 0;
}

int test_integration_v4_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("integration_v4");
    RUN_TEST(test_approval_cache_bash_integration);
    RUN_TEST(test_shell_detect_after_init);
    RUN_TEST(test_shell_detect_in_env);
    RUN_TEST(test_commit_attr_integration);
    RUN_TEST(test_text_encoding_file_read_guard);
    RUN_TEST(test_cmd_canonical_approval_flow);
    RUN_TEST(test_exec_rules_persist_in_memory);
    RUN_TEST(test_shell_type_all_variants);
    RUN_TEST(test_text_encoding_utf8_validation);
    RUN_TEST(test_combined_systems);
    TEST_RETURN_FAILED();
}
