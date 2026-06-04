/*
 * notify.c — Desktop notification system via terminal escape sequences.
 *
 * Supports BEL (audible bell) and OSC 9 (iTerm2/WezTerm/Kitty desktop
 * notification) for long-running task completion alerts.
 */

#include "terminal.h"
#include "nevercode.h"

void term_notify_bel(void) {
    if (!term_is_tty())
        return;
    fputs(NC_NOTIFY_BEL, stdout);
    fflush(stdout);
}

void term_notify_osc9(const char *message) {
    if (!term_is_tty() || !message || !*message)
        return;

    char sanitized[NC_LABEL_MAX];
    size_t j = 0;
    for (size_t i = 0; message[i] && j < sizeof(sanitized) - 1; i++) {
        unsigned char c = (unsigned char)message[i];
        if (c >= 0x20 && c != 0x1b && c != 0x07 && c != 0x9c)
            sanitized[j++] = (char)c;
    }
    sanitized[j] = '\0';

    fprintf(stdout, NC_NOTIFY_OSC9_FMT, sanitized);
    fflush(stdout);
}

void term_notify_task_done(const char *task_desc, double elapsed_s) {
    if (elapsed_s < NC_NOTIFY_LONG_TASK_THRESHOLD_S)
        return;

    char msg[NC_LABEL_MAX];
    if (task_desc && *task_desc) {
        char dur_s[NC_FMT_MAX];
        nc_fmt_elapsed_s(dur_s, sizeof(dur_s), (int)elapsed_s);
        snprintf(msg, sizeof(msg), "%s done (%s)", task_desc, dur_s);
    } else {
        snprintf(msg, sizeof(msg), "Task completed");
    }

    term_notify_osc9(msg);
    term_notify_bel();
}
