#include "test.h"
#include "nevercode.h"
#include "../include/nevercode.h"
#include <string.h>

static int test_init_free(void) {
    diagnostic_entry_t d;
    diagnostic_init(&d);
    ASSERT_INT_EQ(d.severity, DIAG_ERROR);
    ASSERT_INT_EQ(d.line, -1);
    ASSERT_INT_EQ(d.column, -1);
    ASSERT_NULL(d.file);
    ASSERT_NULL(d.message);
    diagnostic_free(&d);
    return 0;
}

static int test_parse_gcc_error(void) {
    const char *input = "main.c:42:5: error: use of undeclared identifier 'foo'\n";
    diagnostic_entry_t diags[8];
    int count = diagnostic_parse_output(input, diags, 8);
    ASSERT(count >= 1);
    ASSERT_STR_EQ(diags[0].file, "main.c");
    ASSERT_INT_EQ(diags[0].line, 42);
    ASSERT_INT_EQ(diags[0].column, 5);
    ASSERT_INT_EQ(diags[0].severity, DIAG_ERROR);
    ASSERT(strstr(diags[0].message, "undeclared identifier") != NULL);
    diagnostics_free(diags, count);
    return 0;
}

static int test_parse_gcc_warning(void) {
    const char *input = "test.c:10:3: warning: unused variable 'x'\n";
    diagnostic_entry_t diags[8];
    int count = diagnostic_parse_output(input, diags, 8);
    ASSERT(count >= 1);
    ASSERT_STR_EQ(diags[0].file, "test.c");
    ASSERT_INT_EQ(diags[0].line, 10);
    ASSERT_INT_EQ(diags[0].severity, DIAG_WARNING);
    ASSERT(strstr(diags[0].message, "unused variable") != NULL);
    diagnostics_free(diags, count);
    return 0;
}

static int test_parse_gcc_note(void) {
    const char *input = "foo.c:5:1: note: declared here\n";
    diagnostic_entry_t diags[8];
    int count = diagnostic_parse_output(input, diags, 8);
    ASSERT(count >= 1);
    ASSERT_INT_EQ(diags[0].severity, DIAG_NOTE);
    diagnostics_free(diags, count);
    return 0;
}

static int test_parse_no_column(void) {
    const char *input = "bar.c:99: error: something went wrong\n";
    diagnostic_entry_t diags[8];
    int count = diagnostic_parse_output(input, diags, 8);
    ASSERT(count >= 1);
    ASSERT_INT_EQ(diags[0].line, 99);
    ASSERT_INT_EQ(diags[0].column, -1);
    diagnostics_free(diags, count);
    return 0;
}

static int test_parse_multiple(void) {
    const char *input = "a.c:1:2: error: first error\n"
                        "b.c:3:4: warning: some warning\n"
                        "c.c:5:6: note: a note\n";
    diagnostic_entry_t diags[8];
    int count = diagnostic_parse_output(input, diags, 8);
    ASSERT_INT_EQ(count, 3);
    ASSERT_STR_EQ(diags[0].file, "a.c");
    ASSERT_STR_EQ(diags[1].file, "b.c");
    ASSERT_STR_EQ(diags[2].file, "c.c");
    diagnostics_free(diags, count);
    return 0;
}

static int test_parse_python_traceback(void) {
    const char *input = "  File \"app.py\", line 42, in main\n";
    diagnostic_entry_t diags[8];
    int count = diagnostic_parse_output(input, diags, 8);
    ASSERT(count >= 1);
    ASSERT_STR_EQ(diags[0].file, "app.py");
    ASSERT_INT_EQ(diags[0].line, 42);
    diagnostics_free(diags, count);
    return 0;
}

static int test_parse_tsc_error(void) {
    const char *input = "src/app.ts(10,5): error TS2304: Cannot find name 'foo'\n";
    diagnostic_entry_t diags[8];
    int count = diagnostic_parse_output(input, diags, 8);
    ASSERT(count >= 1);
    ASSERT_STR_EQ(diags[0].file, "src/app.ts");
    ASSERT_INT_EQ(diags[0].line, 10);
    ASSERT_INT_EQ(diags[0].column, 5);
    ASSERT(strstr(diags[0].message, "Cannot find name") != NULL);
    diagnostics_free(diags, count);
    return 0;
}

static int test_parse_empty(void) {
    int count = diagnostic_parse_output("", NULL, 0);
    ASSERT_INT_EQ(count, 0);
    count = diagnostic_parse_output(NULL, NULL, 0);
    ASSERT_INT_EQ(count, 0);
    return 0;
}

static int test_parse_non_diagnostic(void) {
    const char *input = "Compiling main.c...\nDone.\n";
    diagnostic_entry_t diags[8];
    int count = diagnostic_parse_output(input, diags, 8);
    ASSERT_INT_EQ(count, 0);
    return 0;
}

static int test_suggest_fix(void) {
    diagnostic_entry_t d;
    diagnostic_init(&d);
    d.message = nc_xstrdup("use of undeclared identifier 'x'");
    const char *hint = diagnostic_suggest_fix(&d);
    ASSERT_NOT_NULL(hint);
    ASSERT(strstr(hint, "include") != NULL || strstr(hint, "define") != NULL);
    diagnostic_free(&d);
    return 0;
}

static int test_suggest_unused(void) {
    diagnostic_entry_t d;
    diagnostic_init(&d);
    d.message = nc_xstrdup("unused variable 'temp'");
    const char *hint = diagnostic_suggest_fix(&d);
    ASSERT_NOT_NULL(hint);
    ASSERT(strstr(hint, "Remove") != NULL || strstr(hint, "void") != NULL);
    diagnostic_free(&d);
    return 0;
}

static int test_suggest_no_match(void) {
    diagnostic_entry_t d;
    diagnostic_init(&d);
    d.message = nc_xstrdup("some completely unique error xyz123");
    const char *hint = diagnostic_suggest_fix(&d);
    ASSERT_NULL(hint);
    diagnostic_free(&d);
    return 0;
}

static int test_annotate(void) {
    diagnostic_entry_t diags[2];
    diagnostic_init(&diags[0]);
    diagnostic_init(&diags[1]);
    diags[0].message = nc_xstrdup("implicit declaration of function 'foo'");
    diags[1].message = nc_xstrdup("random unmatched error");
    int ann = diagnostic_annotate_suggestions(diags, 2);
    ASSERT_INT_EQ(ann, 1);
    ASSERT_NOT_NULL(diags[0].suggestion);
    ASSERT_NULL(diags[1].suggestion);
    diagnostics_free(diags, 2);
    return 0;
}

static int test_format(void) {
    diagnostic_entry_t diags[2];
    diagnostic_init(&diags[0]);
    diags[0].file = nc_xstrdup("foo.c");
    diags[0].line = 10;
    diags[0].column = 5;
    diags[0].severity = DIAG_ERROR;
    diags[0].message = nc_xstrdup("test error");

    diagnostic_init(&diags[1]);
    diags[1].file = nc_xstrdup("bar.c");
    diags[1].line = 20;
    diags[1].severity = DIAG_WARNING;
    diags[1].message = nc_xstrdup("test warning");
    diags[1].suggestion = nc_xstrdup("fix it");

    char *out = diagnostic_format(diags, 2);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "foo.c:10:5") != NULL);
    ASSERT(strstr(out, "error") != NULL);
    ASSERT(strstr(out, "bar.c:20") != NULL);
    ASSERT(strstr(out, "Hint: fix it") != NULL);
    free(out);
    diagnostics_free(diags, 2);
    return 0;
}

static int test_format_empty(void) {
    char *out = diagnostic_format(NULL, 0);
    ASSERT_NOT_NULL(out);
    ASSERT(strstr(out, "No diagnostics") != NULL);
    free(out);
    return 0;
}

static int test_count_errors_warnings(void) {
    diagnostic_entry_t diags[3];
    diagnostic_init(&diags[0]);
    diags[0].severity = DIAG_ERROR;
    diagnostic_init(&diags[1]);
    diags[1].severity = DIAG_WARNING;
    diagnostic_init(&diags[2]);
    diags[2].severity = DIAG_ERROR;
    ASSERT_INT_EQ(diagnostic_count_errors(diags, 3), 2);
    ASSERT_INT_EQ(diagnostic_count_warnings(diags, 3), 1);
    diagnostics_free(diags, 3);
    return 0;
}

static int test_summary(void) {
    diagnostic_entry_t diags[2];
    diagnostic_init(&diags[0]);
    diags[0].severity = DIAG_ERROR;
    diagnostic_init(&diags[1]);
    diags[1].severity = DIAG_WARNING;
    char *s = diagnostic_summary(diags, 2);
    ASSERT_NOT_NULL(s);
    ASSERT(strstr(s, "1 error") != NULL);
    ASSERT(strstr(s, "1 warning") != NULL);
    free(s);
    diagnostics_free(diags, 2);
    return 0;
}

static int test_fatal_error(void) {
    const char *input = "inc.h:1:10: fatal error: 'missing.h' file not found\n";
    diagnostic_entry_t diags[8];
    int count = diagnostic_parse_output(input, diags, 8);
    ASSERT(count >= 1);
    ASSERT_INT_EQ(diags[0].severity, DIAG_ERROR);
    ASSERT_INT_EQ(diags[0].line, 1);
    diagnostics_free(diags, count);
    return 0;
}

int test_diagnostic_all(void) {
    TEST_SUITE("diagnostic");
    TEST_COUNTERS;

    RUN_TEST(test_init_free);
    RUN_TEST(test_parse_gcc_error);
    RUN_TEST(test_parse_gcc_warning);
    RUN_TEST(test_parse_gcc_note);
    RUN_TEST(test_parse_no_column);
    RUN_TEST(test_parse_multiple);
    RUN_TEST(test_parse_python_traceback);
    RUN_TEST(test_parse_tsc_error);
    RUN_TEST(test_parse_empty);
    RUN_TEST(test_parse_non_diagnostic);
    RUN_TEST(test_suggest_fix);
    RUN_TEST(test_suggest_unused);
    RUN_TEST(test_suggest_no_match);
    RUN_TEST(test_annotate);
    RUN_TEST(test_format);
    RUN_TEST(test_format_empty);
    RUN_TEST(test_count_errors_warnings);
    RUN_TEST(test_summary);
    RUN_TEST(test_fatal_error);

    TEST_RETURN_FAILED();
}
