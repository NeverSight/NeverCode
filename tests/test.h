#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond);                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define ASSERT_STR_EQ(a, b)                                                                        \
    do {                                                                                           \
        const char *_a = (a), *_b = (b);                                                           \
        if ((_a == NULL && _b != NULL) || (_a != NULL && _b == NULL) ||                            \
            (_a && _b && strcmp(_a, _b) != 0)) {                                                   \
            fprintf(stderr, "  FAIL: %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__,               \
                    _a ? _a : "(null)", _b ? _b : "(null)");                                       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define ASSERT_INT_EQ(a, b)                                                                        \
    do {                                                                                           \
        int _a = (a), _b = (b);                                                                    \
        if (_a != _b) {                                                                            \
            fprintf(stderr, "  FAIL: %s:%d: %d != %d\n", __FILE__, __LINE__, _a, _b);              \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define ASSERT_NOT_NULL(p)                                                                         \
    do {                                                                                           \
        if ((p) == NULL) {                                                                         \
            fprintf(stderr, "  FAIL: %s:%d: expected non-NULL\n", __FILE__, __LINE__);             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define ASSERT_NULL(p)                                                                             \
    do {                                                                                           \
        if ((p) != NULL) {                                                                         \
            fprintf(stderr, "  FAIL: %s:%d: expected NULL\n", __FILE__, __LINE__);                 \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define ASSERT_TRUE(expr) ASSERT(expr)
#define ASSERT_FALSE(expr) ASSERT(!(expr))

#define ASSERT_GT(a, b)                                                                            \
    do {                                                                                           \
        int _a = (a), _b = (b);                                                                    \
        if (!(_a > _b)) {                                                                          \
            fprintf(stderr, "  FAIL: %s:%d: %d not > %d\n", __FILE__, __LINE__, _a, _b);           \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define ASSERT_GE(a, b)                                                                            \
    do {                                                                                           \
        int _a = (a), _b = (b);                                                                    \
        if (!(_a >= _b)) {                                                                         \
            fprintf(stderr, "  FAIL: %s:%d: %d not >= %d\n", __FILE__, __LINE__, _a, _b);          \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define ASSERT_STR_CONTAINS(haystack, needle)                                                      \
    do {                                                                                           \
        const char *_h = (haystack), *_n = (needle);                                               \
        if (!_h || !_n || !strstr(_h, _n)) {                                                       \
            fprintf(stderr, "  FAIL: %s:%d: \"%s\" not found in \"%s\"\n", __FILE__, __LINE__,     \
                    _n ? _n : "(null)", _h ? _h : "(null)");                                       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static inline long _test_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000L + tv.tv_usec;
}

#define RUN_TEST(fn)                                                                               \
    do {                                                                                           \
        fprintf(stdout, "  %-50s", #fn);                                                           \
        fflush(stdout);                                                                            \
        long _t0 = _test_time_us();                                                                \
        int _rc = fn();                                                                            \
        long _dt = _test_time_us() - _t0;                                                          \
        if (_rc == 0) {                                                                            \
            _t_passed++;                                                                           \
            if (_dt > 100000)                                                                      \
                fprintf(stdout, "\033[32mPASS\033[0m (%ld.%ldms)\n", _dt / 1000,                   \
                        (_dt % 1000) / 100);                                                       \
            else                                                                                   \
                fprintf(stdout, "\033[32mPASS\033[0m\n");                                          \
        } else {                                                                                   \
            _t_failed++;                                                                           \
            fprintf(stdout, "\033[31mFAIL\033[0m\n");                                              \
        }                                                                                          \
        _t_run++;                                                                                  \
        _t_time_us += _dt;                                                                         \
    } while (0)

#define TEST_SUITE(name) fprintf(stdout, "\n\033[1m[%s]\033[0m\n", name)

#define TEST_COUNTERS                                                                              \
    int _t_run __attribute__((unused)) = 0;                                                        \
    int _t_passed __attribute__((unused)) = 0;                                                     \
    int _t_failed = 0;                                                                             \
    long _t_time_us __attribute__((unused)) = 0

#define TEST_RETURN_FAILED() return _t_failed

#define TEST_PRINT_SUMMARY(run, pass, fail)                                                        \
    do {                                                                                           \
        fprintf(stdout, "\n\033[1m---\033[0m\n");                                                  \
        fprintf(stdout, "Tests: %d total, \033[32m%d passed\033[0m", (run), (pass));               \
        if ((fail))                                                                                \
            fprintf(stdout, ", \033[31m%d failed\033[0m", (fail));                                 \
        fprintf(stdout, "\n\n");                                                                   \
    } while (0)

#endif /* TEST_H */
