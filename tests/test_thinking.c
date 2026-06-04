#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include "defaults.h"
void think_init(int);
void think_cleanup(void);
int think_feed(const char *, int, char *, int);
int think_block_count(void);
const char *think_block_content(int);
int think_block_len(int);
int think_total_tokens(void);
bool think_over_budget(void);
void think_set_enabled(bool);
bool think_is_enabled(void);
int think_separate(const char *, int, char *, int, char *, int);
static int tr_th = 0, tp_th = 0;
#define RTH(f)                                                                                     \
    do {                                                                                           \
        tr_th++;                                                                                   \
        if (f())                                                                                   \
            tp_th++;                                                                               \
        else                                                                                       \
            fprintf(stderr, "FAIL:%s\n", #f);                                                      \
    } while (0)
static int th_a(void) {
    think_init(1000);
    char v[256];
    int n = think_feed("Hello", 5, v, 256);
    assert(n == 5 && think_block_count() == 0);
    think_cleanup();
    return 1;
}
static int th_b(void) {
    think_init(1000);
    char v[256];
    const char *s = "A<thinking>inner</thinking>B";
    int n = think_feed(s, (int)strlen(s), v, 256);
    v[n] = 0;
    assert(strcmp(v, "AB") == 0 && think_block_count() == 1);
    think_cleanup();
    return 1;
}
static int th_c(void) {
    think_init(1000);
    char v[256];
    const char *s = "A<thinking>t1</thinking>B<thinking>t2</thinking>C";
    int n = think_feed(s, (int)strlen(s), v, 256);
    v[n] = 0;
    assert(strcmp(v, "ABC") == 0 && think_block_count() == 2);
    think_cleanup();
    return 1;
}
static int th_d(void) {
    think_init(5);
    char v[256];
    const char *s = "X<thinking>1234567890</thinking>Y";
    think_feed(s, (int)strlen(s), v, 256);
    assert(think_total_tokens() == 10 && think_over_budget());
    think_cleanup();
    return 1;
}
static int th_e(void) {
    think_init(1000);
    assert(think_is_enabled());
    think_set_enabled(false);
    assert(!think_is_enabled());
    think_cleanup();
    return 1;
}
static int th_f(void) {
    char v[256], t[256];
    const char *s = "Hi<thinking>deep</thinking>Wo";
    think_separate(s, (int)strlen(s), v, 256, t, 256);
    assert(strcmp(v, "HiWo") == 0 && strcmp(t, "deep") == 0);
    return 1;
}
static int th_g(void) {
    think_init(1000);
    char v[256];
    const char *s = "X<thinking></thinking>Y";
    int n = think_feed(s, (int)strlen(s), v, 256);
    v[n] = 0;
    assert(strcmp(v, "XY") == 0);
    think_cleanup();
    return 1;
}
static int th_h(void) {
    think_init(1000);
    assert(!think_block_content(-1) && think_block_len(-1) == 0);
    think_cleanup();
    return 1;
}
static int th_macros(void) {
    assert(THINKING_TAG_OPEN_LEN == (int)strlen(THINKING_TAG_OPEN));
    assert(THINKING_TAG_CLOSE_LEN == (int)strlen(THINKING_TAG_CLOSE));
    assert(THINKING_BLOCK_INIT_CAP > 0);
    return 1;
}
int test_thinking_all(void) {
    tr_th = tp_th = 0;
    RTH(th_a);
    RTH(th_b);
    RTH(th_c);
    RTH(th_d);
    RTH(th_e);
    RTH(th_f);
    RTH(th_g);
    RTH(th_h);
    RTH(th_macros);
    printf("  thinking: %d/%d passed\n", tp_th, tr_th);
    return tr_th - tp_th;
}
