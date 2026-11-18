#include "test.h"
#include "permission.h"
#include "tools.h"
#include "nevercode.h"

static int test_approval_parse_readonly(void) {
    ASSERT_INT_EQ(perm_parse_approval("read-only"), APPROVAL_READONLY);
    ASSERT_INT_EQ(perm_parse_approval("readonly"), APPROVAL_READONLY);
    return 0;
}

static int test_approval_parse_default(void) {
    ASSERT_INT_EQ(perm_parse_approval("default"), APPROVAL_DEFAULT);
    ASSERT_INT_EQ(perm_parse_approval("auto"), APPROVAL_DEFAULT);
    return 0;
}

static int test_approval_parse_full(void) {
    ASSERT_INT_EQ(perm_parse_approval("full-access"), APPROVAL_FULL);
    ASSERT_INT_EQ(perm_parse_approval("full"), APPROVAL_FULL);
    return 0;
}

static int test_approval_parse_invalid(void) {
    ASSERT_INT_EQ(perm_parse_approval("xyz"), -1);
    ASSERT_INT_EQ(perm_parse_approval(NULL), -1);
    return 0;
}

static int test_approval_name(void) {
    ASSERT_STR_EQ(perm_approval_name(APPROVAL_READONLY), "read-only");
    ASSERT_STR_EQ(perm_approval_name(APPROVAL_DEFAULT), "default");
    ASSERT_STR_EQ(perm_approval_name(APPROVAL_FULL), "full-access");
    return 0;
}

static int test_approval_full_trust(void) {
    perm_ctx_t p;
    perm_init(&p);
    perm_set_approval(&p, APPROVAL_FULL);
    ASSERT(p.trust_all == true);
    perm_decision_t d = perm_check(&p, "Bash", NULL);
    ASSERT_INT_EQ(d, PERM_ALLOW);
    perm_free(&p);
    return 0;
}

static int test_approval_readonly_allows_read(void) {
    perm_ctx_t p;
    perm_init(&p);
    perm_set_approval(&p, APPROVAL_READONLY);
    perm_decision_t d = perm_check(&p, "Read", NULL);
    ASSERT_INT_EQ(d, PERM_ALLOW);
    perm_free(&p);
    return 0;
}

static int test_approval_readonly_asks_write(void) {
    perm_ctx_t p;
    perm_init(&p);
    perm_set_approval(&p, APPROVAL_READONLY);
    perm_decision_t d = perm_check(&p, "Bash", NULL);
    ASSERT_INT_EQ(d, PERM_ASK);
    perm_free(&p);
    return 0;
}

static int test_approval_default_mode(void) {
    perm_ctx_t p;
    perm_init(&p);
    perm_set_approval(&p, APPROVAL_DEFAULT);
    ASSERT(p.trust_all == false);
    perm_decision_t d = perm_check(&p, "Read", NULL);
    ASSERT_INT_EQ(d, PERM_ALLOW);
    d = perm_check(&p, "Bash", NULL);
    ASSERT_INT_EQ(d, PERM_ALLOW);
    perm_free(&p);
    return 0;
}

int test_approval_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("approval");

    RUN_TEST(test_approval_parse_readonly);
    RUN_TEST(test_approval_parse_default);
    RUN_TEST(test_approval_parse_full);
    RUN_TEST(test_approval_parse_invalid);
    RUN_TEST(test_approval_name);
    RUN_TEST(test_approval_full_trust);
    RUN_TEST(test_approval_readonly_allows_read);
    RUN_TEST(test_approval_readonly_asks_write);
    RUN_TEST(test_approval_default_mode);

    TEST_RETURN_FAILED();
}
