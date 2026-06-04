#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
void tpool_init(int);
void tpool_cleanup(void);
int tpool_acquire(const char *);
int tpool_complete(int, const char *, int, int);
int tpool_release(int);
int tpool_active_count(void);
int tpool_capacity(void);
bool tpool_slot_busy(int);
const char *tpool_slot_output(int, int *);
int tpool_slot_rc(int);
double tpool_slot_elapsed(int);
static int tr_tp = 0, tp_tp = 0;
#define RTP(f)                                                                                     \
    do {                                                                                           \
        tr_tp++;                                                                                   \
        if (f())                                                                                   \
            tp_tp++;                                                                               \
        else                                                                                       \
            fprintf(stderr, "FAIL:%s\n", #f);                                                      \
    } while (0)
static int tp_a(void) {
    tpool_init(4);
    assert(tpool_capacity() == 4 && tpool_active_count() == 0);
    tpool_cleanup();
    return 1;
}
static int tp_b(void) {
    tpool_init(4);
    int s = tpool_acquire("t");
    assert(s >= 0 && tpool_slot_busy(s));
    tpool_release(s);
    assert(tpool_active_count() == 0);
    tpool_cleanup();
    return 1;
}
static int tp_c(void) {
    tpool_init(4);
    int s = tpool_acquire("t");
    tpool_complete(s, "out", 3, 0);
    int ol;
    const char *o = tpool_slot_output(s, &ol);
    assert(o && ol == 3 && strcmp(o, "out") == 0 && tpool_slot_rc(s) == 0);
    tpool_release(s);
    tpool_cleanup();
    return 1;
}
static int tp_d(void) {
    tpool_init(2);
    int x = tpool_acquire("a"), y = tpool_acquire("b");
    assert(x >= 0 && y >= 0 && tpool_acquire("c") == -1);
    tpool_release(x);
    assert(tpool_acquire("c") >= 0);
    tpool_cleanup();
    return 1;
}
static int tp_e(void) {
    tpool_init(4);
    int s = tpool_acquire("t");
    tpool_complete(s, "e", 1, -1);
    assert(tpool_slot_rc(s) == -1);
    tpool_release(s);
    tpool_cleanup();
    return 1;
}
static int tp_f(void) {
    tpool_init(4);
    int s = tpool_acquire("t");
    assert(tpool_slot_elapsed(s) >= 0.0);
    tpool_release(s);
    tpool_cleanup();
    return 1;
}
static int tp_g(void) {
    tpool_init(4);
    assert(!tpool_slot_busy(-1) && !tpool_slot_busy(999) && tpool_release(-1) == -1);
    tpool_cleanup();
    return 1;
}
int test_tool_pool_all(void) {
    tr_tp = tp_tp = 0;
    RTP(tp_a);
    RTP(tp_b);
    RTP(tp_c);
    RTP(tp_d);
    RTP(tp_e);
    RTP(tp_f);
    RTP(tp_g);
    printf("  tool_pool: %d/%d passed\n", tp_tp, tr_tp);
    return tr_tp - tp_tp;
}
