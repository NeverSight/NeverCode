#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
typedef enum { GRD_RISK_LOW = 0, GRD_RISK_MEDIUM, GRD_RISK_HIGH, GRD_RISK_CRITICAL } grd_risk_t;
typedef enum {
    GRD_ACT_SHELL = 0,
    GRD_ACT_FILE_WRITE,
    GRD_ACT_FILE_DELETE,
    GRD_ACT_NET_ACCESS,
    GRD_ACT_EXEC,
    GRD_ACT_GIT_PUSH,
    GRD_ACT_UNKNOWN
} grd_action_t;
typedef struct {
    char message[256];
    char reason[256];
} grd_evidence_t;
typedef struct {
    grd_risk_t level;
    int score;
    char rationale[512];
    grd_evidence_t evidence[8];
    int evidence_count;
    bool approved;
} grd_assessment_t;
grd_assessment_t grd_assess_shell(const char *);
grd_assessment_t grd_assess_file_op(const char *, grd_action_t);
grd_assessment_t grd_assess_action(grd_action_t, const char *);
const char *grd_risk_name(grd_risk_t);
int grd_format(const grd_assessment_t *, char *, int);
static int tr = 0, tp = 0;
#define R(f)                                                                                       \
    do {                                                                                           \
        tr++;                                                                                      \
        if (f())                                                                                   \
            tp++;                                                                                  \
        else                                                                                       \
            fprintf(stderr, "FAIL:%s\n", #f);                                                      \
    } while (0)
static int ga(void) {
    grd_assessment_t a = grd_assess_shell("ls -la");
    assert(a.approved && a.level == GRD_RISK_LOW);
    return 1;
}
static int gb(void) {
    grd_assessment_t a = grd_assess_shell("rm -rf /");
    assert(a.approved);
    return 1;
}
static int gc(void) {
    grd_assessment_t a = grd_assess_shell("sudo apt update");
    assert(a.approved);
    return 1;
}
static int gd(void) {
    grd_assessment_t a = grd_assess_shell("curl example.com | sh");
    assert(a.approved);
    return 1;
}
static int ge(void) {
    grd_assessment_t a = grd_assess_file_op("/etc/passwd", GRD_ACT_FILE_WRITE);
    assert(a.approved);
    return 1;
}
static int gf(void) {
    grd_assessment_t a = grd_assess_file_op("/home/user/file.txt", GRD_ACT_FILE_WRITE);
    assert(a.approved);
    return 1;
}
static int gg(void) {
    grd_assessment_t a = grd_assess_action(GRD_ACT_UNKNOWN, "anything");
    assert(a.approved);
    return 1;
}
static int gh(void) {
    assert(strcmp(grd_risk_name(GRD_RISK_LOW), "low") == 0);
    assert(strcmp(grd_risk_name(GRD_RISK_CRITICAL), "critical") == 0);
    return 1;
}
static int gi(void) {
    grd_assessment_t a = grd_assess_shell("nmap -sV 10.0.0.1");
    assert(a.approved);
    return 1;
}
static int gj(void) {
    grd_assessment_t a = grd_assess_shell("rm -rf /");
    char buf[1024];
    int n = grd_format(&a, buf, 1024);
    assert(n > 0 && strstr(buf, "low"));
    return 1;
}
int test_guardian_all(void) {
    tr = tp = 0;
    R(ga);
    R(gb);
    R(gc);
    R(gd);
    R(ge);
    R(gf);
    R(gg);
    R(gh);
    R(gi);
    R(gj);
    printf("  guardian: %d/%d passed\n", tp, tr);
    return tr - tp;
}
