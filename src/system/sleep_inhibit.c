/*
 * sleep_inhibit.c — Cross-platform idle-sleep prevention.
 *
 * macOS  : IOKit power assertions (kIOPMAssertionTypePreventUserIdleSystemSleep).
 * Linux  : spawns systemd-inhibit or gnome-session-inhibit as a child process.
 * Other  : no-op stubs.
 */

#include "nevercode.h"
#include "defaults.h"

/* ─── macOS: IOKit power assertions ─── */
#ifdef __APPLE__
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <CoreFoundation/CoreFoundation.h>

static IOPMAssertionID g_assertion_id = 0;
static bool g_assertion_held = false;

void sleep_inhibit_acquire(void) {
    if (g_assertion_held)
        return;
    CFStringRef reason = CFSTR(NC_INHIBIT_REASON);
    IOReturn ret = IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleSystemSleep,
                                               kIOPMAssertionLevelOn, reason, &g_assertion_id);
    if (ret == kIOReturnSuccess)
        g_assertion_held = true;
}

void sleep_inhibit_release(void) {
    if (!g_assertion_held)
        return;
    IOPMAssertionRelease(g_assertion_id);
    g_assertion_id = 0;
    g_assertion_held = false;
}

bool sleep_inhibit_active(void) {
    return g_assertion_held;
}

/* ─── Linux: systemd-inhibit / gnome-session-inhibit ─── */
#elif defined(__linux__)

/* NC_INHIBIT_REASON from defaults.h */

typedef enum { BACKEND_NONE, BACKEND_SYSTEMD, BACKEND_GNOME } linux_backend_t;

static pid_t g_inhibit_pid = 0;
static linux_backend_t g_linux_backend = BACKEND_NONE;
static bool g_backend_probed = false;

static linux_backend_t probe_backend(void) {
    if (nc_cmd_exists("systemd-inhibit"))
        return BACKEND_SYSTEMD;
    if (nc_cmd_exists("gnome-session-inhibit"))
        return BACKEND_GNOME;
    return BACKEND_NONE;
}

void sleep_inhibit_acquire(void) {
    if (g_inhibit_pid > 0) {
        int status;
        pid_t w = waitpid(g_inhibit_pid, &status, WNOHANG);
        if (w == 0)
            return; /* still running */
        g_inhibit_pid = 0;
    }

    if (!g_backend_probed) {
        g_linux_backend = probe_backend();
        g_backend_probed = true;
    }
    if (g_linux_backend == BACKEND_NONE)
        return;

    pid_t pid = fork();
    if (pid < 0)
        return;
    if (pid == 0) {
        nc_child_setup();
        if (g_linux_backend == BACKEND_SYSTEMD) {
            execlp("systemd-inhibit", "systemd-inhibit", "--what=idle", "--who=" NC_PRODUCT_NAME,
                   "--why=" NC_INHIBIT_REASON, "sleep", SLEEP_INHIBIT_DURATION_S, (char *)NULL);
        } else {
            execlp("gnome-session-inhibit", "gnome-session-inhibit", "--app-id", NC_PRODUCT_NAME,
                   "--reason", NC_INHIBIT_REASON, "--inhibit", "idle", "sleep",
                   SLEEP_INHIBIT_DURATION_S, (char *)NULL);
        }
        _exit(127);
    }
    g_inhibit_pid = pid;
}

void sleep_inhibit_release(void) {
    if (g_inhibit_pid <= 0)
        return;
    kill(g_inhibit_pid, SIGTERM);
    int st;
    waitpid(g_inhibit_pid, &st, 0);
    g_inhibit_pid = 0;
}

bool sleep_inhibit_active(void) {
    if (g_inhibit_pid <= 0)
        return false;
    int status;
    pid_t w = waitpid(g_inhibit_pid, &status, WNOHANG);
    if (w != 0) {
        g_inhibit_pid = 0;
        return false;
    }
    return true;
}

/* ─── Unsupported platforms: no-op ─── */
#else

void sleep_inhibit_acquire(void) {}
void sleep_inhibit_release(void) {}
bool sleep_inhibit_active(void) {
    return false;
}

#endif

/* ─── Common control layer ─── */

static bool g_sleep_inhibit_enabled = true;
static bool g_turn_running = false;

void sleep_inhibit_set_enabled(bool enabled) {
    g_sleep_inhibit_enabled = enabled;
    if (!enabled)
        sleep_inhibit_release();
}

void sleep_inhibit_set_turn(bool running) {
    g_turn_running = running;
    if (!g_sleep_inhibit_enabled) {
        sleep_inhibit_release();
        return;
    }
    if (running)
        sleep_inhibit_acquire();
    else
        sleep_inhibit_release();
}

bool sleep_inhibit_is_turn_running(void) {
    return g_turn_running;
}
