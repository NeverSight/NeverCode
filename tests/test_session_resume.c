#include "test.h"
#include "nevercode.h"
#include <stdlib.h>
#include <string.h>

/* Declarations from session_resume.c */
extern void session_resume_init(void);
extern void session_resume_free(void);
extern bool session_resume_ready(void);
extern const char *session_resume_summary(void);
extern const char *session_resume_reference(void);
extern int session_resume_msgs_restored(void);
extern long session_resume_elapsed_ms(void);
extern bool session_resume_memory_loaded(void);
extern char *session_resume_format(void);

static int test_init_defaults(void) {
    session_resume_init();
    ASSERT_FALSE(session_resume_ready());
    ASSERT_NULL(session_resume_summary());
    ASSERT_NULL(session_resume_reference());
    ASSERT_INT_EQ(0, session_resume_msgs_restored());
    ASSERT_INT_EQ(0, (int)session_resume_elapsed_ms());
    ASSERT_FALSE(session_resume_memory_loaded());
    session_resume_free();
    return 0;
}

static int test_format_idle(void) {
    session_resume_init();
    char *fmt = session_resume_format();
    ASSERT_NOT_NULL(fmt);
    ASSERT_STR_CONTAINS(fmt, "idle");
    ASSERT_STR_CONTAINS(fmt, "(none)");
    free(fmt);
    session_resume_free();
    return 0;
}

static int test_double_init(void) {
    session_resume_init();
    session_resume_init();
    ASSERT_FALSE(session_resume_ready());
    session_resume_free();
    return 0;
}

static int test_free_idempotent(void) {
    session_resume_init();
    session_resume_free();
    session_resume_free();
    return 0;
}

int test_session_resume_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("session_resume");
    RUN_TEST(test_init_defaults);
    RUN_TEST(test_format_idle);
    RUN_TEST(test_double_init);
    RUN_TEST(test_free_idempotent);
    return _t_failed;
}

#if 0 // disabled for unified build
#if 0
int main(void)
{
    TEST_COUNTERS;
    TEST_SUITE("session_resume");
    RUN_TEST(test_init_defaults);
    RUN_TEST(test_format_idle);
    RUN_TEST(test_double_init);
    RUN_TEST(test_free_idempotent);
    TEST_PRINT_SUMMARY(_t_run, _t_passed, _t_failed);
    return _t_failed;
}
#endif
#endif
