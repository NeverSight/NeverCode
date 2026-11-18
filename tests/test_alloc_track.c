/*
 * test_alloc_track.c — Tests for debug allocation tracker
 *
 * The tracker is compile-gated by NEVERCODE_DEBUG_ALLOC.  When the flag
 * is off, all functions are no-ops, so these tests simply verify the
 * no-op path returns sane defaults.
 */

#include "test.h"
#include <stdint.h>
#include <string.h>
#include "nevercode.h"

#ifdef NEVERCODE_DEBUG_ALLOC
#define ALLOC_ACTIVE 1
#else
#define ALLOC_ACTIVE 0
#endif

static int test_init_free(void) {
    alloc_track_init();
    ASSERT_INT_EQ(alloc_track_leak_count(), 0);
    ASSERT(alloc_track_active_bytes() == 0);
    ASSERT(alloc_track_high_water() == 0);
    alloc_track_free();
    return 0;
}

static int test_record_and_release(void) {
    alloc_track_init();
    char *p = malloc(128);
    alloc_track_record(p, 128, __FILE__, __LINE__);
    ASSERT_INT_EQ(alloc_track_leak_count(), ALLOC_ACTIVE);
    ASSERT(alloc_track_active_bytes() == (ALLOC_ACTIVE ? 128u : 0u));

    alloc_track_release(p);
    ASSERT_INT_EQ(alloc_track_leak_count(), 0);
    ASSERT(alloc_track_active_bytes() == 0);
    free(p);
    alloc_track_free();
    return 0;
}

static int test_high_water(void) {
    alloc_track_init();
    char *a = malloc(100);
    char *b = malloc(200);
    alloc_track_record(a, 100, __FILE__, __LINE__);
    alloc_track_record(b, 200, __FILE__, __LINE__);
    ASSERT(alloc_track_high_water() == (ALLOC_ACTIVE ? 300u : 0u));

    alloc_track_release(a);
    ASSERT(alloc_track_high_water() == (ALLOC_ACTIVE ? 300u : 0u));

    alloc_track_release(b);
    ASSERT(alloc_track_active_bytes() == 0);

    free(a);
    free(b);
    alloc_track_free();
    return 0;
}

static int test_update_realloc(void) {
    alloc_track_init();
    char *p = malloc(64);
    alloc_track_record(p, 64, __FILE__, __LINE__);
    ASSERT_INT_EQ(alloc_track_leak_count(), ALLOC_ACTIVE);

    void *volatile p_old = p; /* volatile read: only the address bits are wanted */
    char *q = realloc(p, 256);
    alloc_track_update(p_old, q, 256, __FILE__, __LINE__);
    ASSERT_INT_EQ(alloc_track_leak_count(), ALLOC_ACTIVE);
    ASSERT(alloc_track_active_bytes() == (ALLOC_ACTIVE ? 256u : 0u));

    alloc_track_release(q);
    ASSERT_INT_EQ(alloc_track_leak_count(), 0);
    free(q);
    alloc_track_free();
    return 0;
}

static int test_report(void) {
    alloc_track_init();
    char *p = malloc(42);
    alloc_track_record(p, 42, "fake.c", 99);

    char *r = alloc_track_report(10);
    ASSERT_NOT_NULL(r);
    if (ALLOC_ACTIVE)
        ASSERT_STR_CONTAINS(r, "42 bytes");
    else
        ASSERT_STR_CONTAINS(r, "disabled");
    free(r);

    alloc_track_release(p);
    free(p);
    alloc_track_free();
    return 0;
}

static int test_null_safety(void) {
    alloc_track_init();
    alloc_track_record(NULL, 0, NULL, 0);
    alloc_track_release(NULL);
    alloc_track_update(NULL, NULL, 0, NULL, 0);
    ASSERT_INT_EQ(alloc_track_leak_count(), 0);
    alloc_track_free();
    return 0;
}

static int test_multiple_allocations(void) {
    alloc_track_init();
    void *ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = malloc(100);
        alloc_track_record(ptrs[i], 100, __FILE__, __LINE__);
    }
    ASSERT_INT_EQ(alloc_track_leak_count(), ALLOC_ACTIVE ? 10 : 0);
    ASSERT(alloc_track_active_bytes() == (ALLOC_ACTIVE ? 1000u : 0u));

    for (int i = 0; i < 10; i++) {
        alloc_track_release(ptrs[i]);
        free(ptrs[i]);
    }
    ASSERT_INT_EQ(alloc_track_leak_count(), 0);
    alloc_track_free();
    return 0;
}

static int test_report_no_leaks(void) {
    alloc_track_init();
    char *r = alloc_track_report(10);
    ASSERT_NOT_NULL(r);
    ASSERT(strlen(r) > 0);
    free(r);
    alloc_track_free();
    return 0;
}

int test_alloc_track_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("alloc_track");
    RUN_TEST(test_init_free);
    RUN_TEST(test_record_and_release);
    RUN_TEST(test_high_water);
    RUN_TEST(test_update_realloc);
    RUN_TEST(test_report);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_multiple_allocations);
    RUN_TEST(test_report_no_leaks);
    TEST_RETURN_FAILED();
}
