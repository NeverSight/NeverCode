#include "nevercode.h"
#include "defaults.h"

static bool g_suppressed = false;
static int g_turns_remaining = 0;

void compact_warn_suppress(void) {
    g_suppressed = true;
    g_turns_remaining = COMPACT_WARN_SUPPRESS_TURNS;
}

void compact_warn_clear(void) {
    g_suppressed = false;
    g_turns_remaining = 0;
}

bool compact_warn_is_suppressed(void) {
    if (!g_suppressed)
        return false;
    if (g_turns_remaining > 0) {
        g_turns_remaining--;
        return true;
    }
    g_suppressed = false;
    return false;
}
