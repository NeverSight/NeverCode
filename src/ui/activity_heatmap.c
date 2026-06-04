/*
 * activity_heatmap.c — GitHub-style terminal activity heatmap.
 *
 * Renders a 7-row (Mon..Sun) × N-week grid of Unicode block
 * characters coloured by message-count percentile.  Used by
 * /insights to give a visual overview of session activity.
 */

#include "nevercode.h"

static const char *intensity_block(int count, int p25, int p50, int p75) {
    if (count <= 0)
        return "\xe2\x96\x91"; /* ░ empty */
    if (count <= p25)
        return "\xe2\x96\x92"; /* ▒ low */
    if (count <= p50)
        return "\xe2\x96\x93"; /* ▓ medium */
    if (count <= p75)
        return "\xe2\x96\x88"; /* █ high */
    return "\xe2\x96\x88";     /* █ very high */
}

char *activity_heatmap_render(const int *daily_counts, int num_days, int term_width) {
    if (!daily_counts || num_days <= 0)
        return NULL;

    int max_weeks = (term_width - HEATMAP_LABEL_WIDTH) > 0 ? (term_width - HEATMAP_LABEL_WIDTH)
                                                           : HEATMAP_MIN_WEEKS;
    if (max_weeks > HEATMAP_MAX_WEEKS)
        max_weeks = HEATMAP_MAX_WEEKS;

    /* Compute percentiles from non-zero counts */
    int sorted[HEATMAP_MAX_DAYS];
    int ns = 0;
    for (int i = 0; i < num_days && i < HEATMAP_MAX_DAYS; i++) {
        if (daily_counts[i] > 0 && ns < HEATMAP_MAX_DAYS)
            sorted[ns++] = daily_counts[i];
    }

    if (ns == 0)
        return nc_xstrdup("  (no activity data)\n");

    /* Simple sort for percentiles */
    for (int i = 0; i < ns - 1; i++)
        for (int j = i + 1; j < ns; j++)
            if (sorted[j] < sorted[i]) {
                int tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }

    int p25 = sorted[ns / 4];
    int p50 = sorted[ns / 2];
    int p75 = sorted[(ns * 3) / 4];
    if (p25 < 1)
        p25 = 1;
    if (p50 <= p25)
        p50 = p25 + 1;
    if (p75 <= p50)
        p75 = p50 + 1;

    /* Build grid: 7 rows × max_weeks columns */
    int weeks = num_days / 7;
    if (weeks > max_weeks)
        weeks = max_weeks;
    if (weeks < 1)
        weeks = 1;

    int start_offset = num_days - (weeks * 7);
    if (start_offset < 0)
        start_offset = 0;

    strbuf_t sb;
    strbuf_init(&sb);

    static const char *day_labels[] = {"Mon", "   ", "Wed", "   ", "Fri", "   ", "Sun"};

    for (int row = 0; row < 7; row++) {
        strbuf_appendf(&sb, "%s ", day_labels[row]);
        for (int w = 0; w < weeks; w++) {
            int idx = start_offset + w * 7 + row;
            int cnt = (idx >= 0 && idx < num_days) ? daily_counts[idx] : 0;
            strbuf_append_str(&sb, intensity_block(cnt, p25, p50, p75));
        }
        strbuf_append_str(&sb, "\n");
    }

    strbuf_appendf(&sb,
                   "    \xe2\x96\x91=0  \xe2\x96\x92=low  \xe2\x96\x93=med  \xe2\x96\x88=high\n");

    return strbuf_detach(&sb);
}
