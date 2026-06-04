#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
typedef enum {
    FRAG_IDENTITY = 0,
    FRAG_PROJECT_DOC,
    FRAG_USER_INSTRUCTIONS,
    FRAG_SKILL,
    FRAG_RUNTIME,
    FRAG_TOOL_HINTS,
    FRAG_SESSION_CTX
} frag_kind_t;
void pb_init(int);
void pb_cleanup(void);
int pb_add(const char *, const char *, frag_kind_t, int);
int pb_remove(const char *);
int pb_build(char *, int);
int pb_fragment_count(void);
int pb_total_len(void);
bool pb_has(const char *);
static int tr = 0, tp = 0;
#define R(f)                                                                                       \
    do {                                                                                           \
        tr++;                                                                                      \
        if (f())                                                                                   \
            tp++;                                                                                  \
        else                                                                                       \
            fprintf(stderr, "FAIL:%s\n", #f);                                                      \
    } while (0)
static int pba(void) {
    pb_init(10000);
    assert(pb_fragment_count() == 0);
    pb_cleanup();
    return 1;
}
static int pbb(void) {
    pb_init(10000);
    assert(pb_add("identity", "You are nevercode.", FRAG_IDENTITY, 100) == 0);
    assert(pb_fragment_count() == 1 && pb_has("identity"));
    pb_cleanup();
    return 1;
}
static int pbc(void) {
    pb_init(10000);
    pb_add("a", "AAA", FRAG_IDENTITY, 10);
    pb_add("b", "BBB", FRAG_RUNTIME, 5);
    char buf[1024];
    int n = pb_build(buf, 1024);
    assert(n > 0 && strstr(buf, "AAA") && strstr(buf, "BBB"));
    pb_cleanup();
    return 1;
}
static int pbd(void) {
    pb_init(10000);
    pb_add("x", "XYZ", FRAG_IDENTITY, 10);
    pb_remove("x");
    assert(!pb_has("x") && pb_fragment_count() == 0);
    pb_cleanup();
    return 1;
}
static int pbe(void) {
    pb_init(10000);
    pb_add("id", "ID", FRAG_IDENTITY, 100);
    pb_add("doc", "DOC", FRAG_PROJECT_DOC, 50);
    pb_add("rt", "RT", FRAG_RUNTIME, 10);
    char buf[1024];
    int n = pb_build(buf, 1024);
    assert(n > 0);
    char *p1 = strstr(buf, "ID");
    char *p2 = strstr(buf, "DOC");
    char *p3 = strstr(buf, "RT");
    assert(p1 && p2 && p3 && p1 < p2 && p2 < p3);
    pb_cleanup();
    return 1;
}
static int pbf(void) {
    pb_init(10000);
    assert(pb_add("dup", "A", FRAG_IDENTITY, 1) == 0);
    assert(pb_add("dup", "B", FRAG_IDENTITY, 1) == -2);
    pb_cleanup();
    return 1;
}
static int pbg(void) {
    pb_init(10000);
    pb_add("a", "Hello", FRAG_IDENTITY, 1);
    assert(pb_total_len() == 5);
    pb_cleanup();
    return 1;
}
int test_prompt_builder_all(void) {
    tr = tp = 0;
    R(pba);
    R(pbb);
    R(pbc);
    R(pbd);
    R(pbe);
    R(pbf);
    R(pbg);
    printf("  prompt_builder: %d/%d passed\n", tp, tr);
    return tr - tp;
}
