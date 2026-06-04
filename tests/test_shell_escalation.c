#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
typedef enum { ESCALATE_NONE = 0, ESCALATE_DETECTED, ESCALATE_DANGER } escalation_level_t;
typedef struct {
    escalation_level_t level;
    const char *matched_cmd;
    int prefix_offset;
} escalation_result_t;
escalation_result_t check_escalation(const char *);
const char *strip_escalation_prefix(const char *);
bool is_destructive_command(const char *);
static int tr_se = 0, tp_se = 0;
#define RSE(f)                                                                                     \
    do {                                                                                           \
        tr_se++;                                                                                   \
        if (f())                                                                                   \
            tp_se++;                                                                               \
        else                                                                                       \
            fprintf(stderr, "FAIL:%s\n", #f);                                                      \
    } while (0)
static int se_a(void) {
    assert(check_escalation("ls").level == ESCALATE_NONE);
    return 1;
}
static int se_b(void) {
    assert(check_escalation("sudo apt install vim").level == ESCALATE_DETECTED);
    return 1;
}
static int se_c(void) {
    assert(check_escalation("doas pkg install vim").level == ESCALATE_DETECTED);
    return 1;
}
static int se_d(void) {
    assert(check_escalation("echo x|sudo tee /f").level == ESCALATE_DETECTED);
    return 1;
}
static int se_e(void) {
    assert(check_escalation("echo hi && sudo apt update").level == ESCALATE_DETECTED);
    return 1;
}
static int se_f(void) {
    assert(check_escalation("rm -rf /").level == ESCALATE_DANGER);
    return 1;
}
static int se_g(void) {
    assert(strcmp(strip_escalation_prefix("sudo apt install vim"), "apt install vim") == 0);
    return 1;
}
static int se_h(void) {
    assert(strcmp(strip_escalation_prefix("ls -la"), "ls -la") == 0);
    return 1;
}
static int se_i(void) {
    assert(is_destructive_command("rm -rf /"));
    assert(!is_destructive_command("ls"));
    return 1;
}
static int se_j(void) {
    assert(check_escalation(NULL).level == ESCALATE_NONE);
    return 1;
}
static int se_k(void) {
    assert(check_escalation("   sudo reboot").level == ESCALATE_DETECTED);
    return 1;
}
int test_shell_escalation_all(void) {
    tr_se = tp_se = 0;
    RSE(se_a);
    RSE(se_b);
    RSE(se_c);
    RSE(se_d);
    RSE(se_e);
    RSE(se_f);
    RSE(se_g);
    RSE(se_h);
    RSE(se_i);
    RSE(se_j);
    RSE(se_k);
    printf("  shell_escalation: %d/%d passed\n", tp_se, tr_se);
    return tr_se - tp_se;
}
