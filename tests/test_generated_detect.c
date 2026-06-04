/*
 * test_generated_detect.c — Tests for generated/vendored file detection.
 */
#include "test.h"
#include "nevercode.h"

static int t_lock_files(void) {
    ASSERT(generated_detect("package-lock.json"));
    ASSERT(generated_detect("yarn.lock"));
    ASSERT(generated_detect("Cargo.lock"));
    ASSERT(generated_detect("go.sum"));
    ASSERT(generated_is_lock_file("poetry.lock"));
    return 0;
}

static int t_minified(void) {
    ASSERT(generated_detect("app.min.js"));
    ASSERT(generated_detect("styles.min.css"));
    ASSERT(generated_detect("vendor.bundle.js"));
    return 0;
}

static int t_vendor_dirs(void) {
    ASSERT(generated_detect("project/node_modules/react/index.js"));
    ASSERT(generated_detect("src/dist/bundle.js"));
    ASSERT(generated_detect("project/.git/objects/abc"));
    ASSERT(generated_is_vendor("/path/vendor/lib.c"));
    return 0;
}

static int t_normal_files(void) {
    ASSERT(!generated_detect("src/main.c"));
    ASSERT(!generated_detect("README.md"));
    ASSERT(!generated_detect("tests/test_foo.c"));
    ASSERT(!generated_detect("include/nevercode.h"));
    return 0;
}

static int t_null_safety(void) {
    ASSERT(!generated_detect(NULL));
    ASSERT(!generated_detect(""));
    ASSERT(!generated_is_lock_file(NULL));
    ASSERT(!generated_is_vendor(NULL));
    return 0;
}

int test_generated_detect_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("generated_detect");
    RUN_TEST(t_lock_files);
    RUN_TEST(t_minified);
    RUN_TEST(t_vendor_dirs);
    RUN_TEST(t_normal_files);
    RUN_TEST(t_null_safety);
    TEST_RETURN_FAILED();
}
