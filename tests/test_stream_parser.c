#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
typedef struct {
    unsigned char pending[4];
    int pending_len;
    int expected_len;
} utf8_stream_t;
void utf8_stream_init(utf8_stream_t *);
int utf8_stream_feed(utf8_stream_t *, const char *, int, char *, int);
int strip_citations(const char *, int, char *, int);
typedef enum { PLAN_SEG_TEXT, PLAN_SEG_PLAN } plan_seg_type_t;
typedef struct {
    plan_seg_type_t type;
    const char *start;
    int len;
} plan_segment_t;
int extract_plan_segments(const char *, int, plan_segment_t *, int);
typedef struct {
    const char *tag_name;
    int tag_len;
} inline_tag_spec_t;
typedef struct {
    const char *content;
    int content_len;
    const char *tag_name;
} extracted_tag_t;
bool extract_inline_tag(const char *, int, const inline_tag_spec_t *, extracted_tag_t *);
int strip_inline_tags(const char *, int, const inline_tag_spec_t *, char *, int);
static int tr_sp = 0, tp_sp = 0;
#define RSP(f)                                                                                     \
    do {                                                                                           \
        tr_sp++;                                                                                   \
        if (f())                                                                                   \
            tp_sp++;                                                                               \
        else                                                                                       \
            fprintf(stderr, "FAIL:%s\n", #f);                                                      \
    } while (0)
static int sp_a(void) {
    utf8_stream_t u;
    utf8_stream_init(&u);
    char o[64];
    assert(utf8_stream_feed(&u, "hello", 5, o, 64) == 5);
    return 1;
}
static int sp_b(void) {
    utf8_stream_t u;
    utf8_stream_init(&u);
    char o[64];
    utf8_stream_feed(&u, "\xe4", 1, o, 64);
    utf8_stream_feed(&u, "\xbd", 1, o, 64);
    assert(utf8_stream_feed(&u, "\xa0", 1, o, 64) == 3);
    return 1;
}
static int sp_c(void) {
    const char *s = "Hi\xe3\x80\x90x\xe3\x80\x91Wo";
    char o[64];
    int n = strip_citations(s, (int)strlen(s), o, 64);
    o[n] = 0;
    assert(strcmp(o, "HiWo") == 0);
    return 1;
}
static int sp_d(void) {
    const char *s = "plain";
    char o[64];
    int n = strip_citations(s, 5, o, 64);
    o[n] = 0;
    assert(strcmp(o, "plain") == 0);
    return 1;
}
static int sp_e(void) {
    plan_segment_t sg[8];
    assert(extract_plan_segments("text", 4, sg, 8) == 1 && sg[0].type == PLAN_SEG_TEXT);
    return 1;
}
static int sp_f(void) {
    plan_segment_t sg[8];
    assert(extract_plan_segments("A<plan>B</plan>C", 16, sg, 8) == 3);
    return 1;
}
static int sp_g(void) {
    inline_tag_spec_t sp = {"s", 1};
    extracted_tag_t et;
    assert(extract_inline_tag("X<s>hi</s>Y", 11, &sp, &et) && et.content_len == 2);
    return 1;
}
static int sp_h(void) {
    inline_tag_spec_t sp = {"s", 1};
    char o[64];
    int n = strip_inline_tags("A<s>B</s>C", 10, &sp, o, 64);
    o[n] = 0;
    assert(strcmp(o, "AC") == 0);
    return 1;
}
static int sp_i(void) {
    inline_tag_spec_t sp = {"x", 1};
    extracted_tag_t et;
    assert(!extract_inline_tag("no", 2, &sp, &et));
    return 1;
}
int test_stream_parser_all(void) {
    tr_sp = tp_sp = 0;
    RSP(sp_a);
    RSP(sp_b);
    RSP(sp_c);
    RSP(sp_d);
    RSP(sp_e);
    RSP(sp_f);
    RSP(sp_g);
    RSP(sp_h);
    RSP(sp_i);
    printf("  stream_parser: %d/%d passed\n", tp_sp, tr_sp);
    return tr_sp - tp_sp;
}
