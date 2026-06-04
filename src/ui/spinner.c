/*
 * spinner.c — Animated loading indicator with rotating verb labels.
 *
 * Braille-dot spinner with time-based animation and periodic verb
 * rotation for lively UX during API calls and tool execution.
 */

#include "terminal.h"
#include "nevercode.h"

static const char *SPINNER_FRAMES[NC_SPINNER_FRAME_COUNT] = {"⠋", "⠙", "⠹", "⠸", "⠼",
                                                             "⠴", "⠦", "⠧", "⠇", "⠏"};

static const char *SPINNER_VERBS[NC_SPINNER_VERB_COUNT] = {
    "Thinking",    "Processing",  "Analyzing",    "Computing",     "Synthesizing", "Generating",
    "Evaluating",  "Iterating",   "Architecting", "Orchestrating", "Inferring",    "Reasoning",
    "Composing",   "Formulating", "Distilling",   "Devising",      "Assembling",   "Weaving",
    "Calibrating", "Mapping",     "Resolving",    "Diagnosing",    "Probing",      "Decoding",
    "Sculpting",   "Forging",     "Refining",     "Charting",      "Unraveling",   "Constructing",
    "Harmonizing", "Navigating",
};

static const char *COMPLETION_VERBS[NC_COMPLETION_VERB_COUNT] = {
    "Worked", "Processed", "Computed", "Executed", "Ran", "Finished", "Completed", "Delivered",
};

/* alias for the public inline — avoids shadowing while keeping callsites short */
#define ts_diff_ms_spin nc_ts_diff_ms

void spinner_init(spinner_t *sp) {
    if (!sp)
        return;
    memset(sp, 0, sizeof(*sp));
}

void spinner_start(spinner_t *sp) {
    if (!sp)
        return;
    sp->active = true;
    sp->frame = 0;
    clock_gettime(CLOCK_MONOTONIC, &sp->last_tick);
}

void spinner_stop(spinner_t *sp) {
    if (!sp)
        return;
    if (sp->active && term_is_tty()) {
        fputs(T_CLEAR_LINE, stdout);
        fflush(stdout);
    }
    sp->active = false;
}

const char *spinner_tick(spinner_t *sp) {
    if (!sp || !sp->active)
        return NULL;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    int64_t elapsed = ts_diff_ms_spin(&now, &sp->last_tick);
    if (elapsed >= NC_SPINNER_INTERVAL_MS) {
        sp->frame = (sp->frame + 1) % NC_SPINNER_FRAME_COUNT;
        sp->last_tick = now;
    }

    return SPINNER_FRAMES[sp->frame];
}

const char *spinner_verb(void) {
    static int idx = -1;
    static struct timespec last_change = {0, 0};
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t gap = ts_diff_ms_spin(&now, &last_change);
    if (idx < 0 || gap >= NC_SPINNER_VERB_INTERVAL_MS) {
        idx = (int)nc_rand_uniform(NC_SPINNER_VERB_COUNT);
        last_change = now;
    }
    return SPINNER_VERBS[idx];
}

const char *spinner_completion_verb(void) {
    return COMPLETION_VERBS[nc_rand_uniform(NC_COMPLETION_VERB_COUNT)];
}

void spinner_render(spinner_t *sp, const char *label, double elapsed_s) {
    const char *frame = spinner_tick(sp);
    if (!frame)
        return;
    if (label && elapsed_s > 0) {
        fprintf(stdout, T_CLEAR_LINE " %s %s (%.1fs)", frame, label, elapsed_s);
    } else if (label) {
        fprintf(stdout, T_CLEAR_LINE " %s %s", frame, label);
    } else {
        const char *verb = spinner_verb();
        if (elapsed_s > 0)
            fprintf(stdout, T_CLEAR_LINE " %s %s (%.1fs)", frame, verb, elapsed_s);
        else
            fprintf(stdout, T_CLEAR_LINE " %s %s", frame, verb);
    }
    fflush(stdout);
}
