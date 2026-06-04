#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <unistd.h>
void pm_init(void);
int pm_spawn(const char *, const char *);
int pm_write_stdin(int, const char *, int);
int pm_read_stdout(int, char *, int);
int pm_kill(int, int);
int pm_wait(int);
int pm_check(int);
void pm_release(int);
int pm_active(void);
bool pm_at_warning(void);
bool pm_is_running(int);
int pm_exit_code(int);
const char *pm_command(int);
int pm_list_active(int *, int);
int pm_clamp_yield(int);
static int tr = 0, tp = 0;
#define R(f)                                                                                       \
    do {                                                                                           \
        tr++;                                                                                      \
        if (f())                                                                                   \
            tp++;                                                                                  \
        else                                                                                       \
            fprintf(stderr, "FAIL:%s\n", #f);                                                      \
    } while (0)
static int pa(void) {
    pm_init();
    assert(pm_active() == 0);
    return 1;
}
static int pb(void) {
    pm_init();
    int id = pm_spawn("echo hello", NULL);
    assert(id > 0);
    usleep(100000);
    int rc = pm_wait(id);
    assert(rc == 0);
    pm_release(id);
    return 1;
}
static int pc(void) {
    pm_init();
    int id = pm_spawn("echo test_out", NULL);
    assert(id > 0);
    usleep(100000);
    char buf[256];
    int n = pm_read_stdout(id, buf, 256);
    assert(n > 0);
    pm_wait(id);
    pm_release(id);
    return 1;
}
static int pd(void) {
    pm_init();
    int id = pm_spawn("sleep 10", NULL);
    assert(pm_is_running(id));
    pm_kill(id, 9);
    pm_wait(id);
    pm_release(id);
    assert(pm_active() == 0);
    return 1;
}
static int pe(void) {
    assert(pm_clamp_yield(100) == 250);
    assert(pm_clamp_yield(5000) == 5000);
    assert(pm_clamp_yield(99999) == 30000);
    return 1;
}
static int pf(void) {
    pm_init();
    int id = pm_spawn("exit 42", NULL);
    usleep(100000);
    int rc = pm_wait(id);
    assert(rc == 42);
    pm_release(id);
    return 1;
}
static int pg(void) {
    pm_init();
    int id = pm_spawn("echo hi", NULL);
    assert(pm_command(id) != NULL && strstr(pm_command(id), "echo"));
    pm_wait(id);
    pm_release(id);
    return 1;
}
int test_process_mgr_all(void) {
    tr = tp = 0;
    R(pa);
    R(pb);
    R(pc);
    R(pd);
    R(pe);
    R(pf);
    R(pg);
    printf("  process_mgr: %d/%d passed\n", tp, tr);
    return tr - tp;
}
