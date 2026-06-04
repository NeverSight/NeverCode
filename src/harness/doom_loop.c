/*
 * doom_loop.c — Repeated tool-call pattern detection.
 *
 * Tracks a sliding window of recent tool invocations (name + input hash).
 * When the same call appears DOOM_LOOP_THRESHOLD times within the window,
 * the detector fires and the harness can inject a corrective nudge or
 * break the agent loop.
 */

#include "nevercode.h"
#include "defaults.h"

typedef struct {
    uint32_t hash;
    char tool_name[DYN_TOOL_NAME_MAX];
} doom_entry_t;

static struct {
    doom_entry_t ring[DOOM_LOOP_WINDOW];
    int head;
    int count;
    int fire_count;
    bool enabled;
} g_doom;

static uint32_t fnv1a(const char *data, size_t len) {
    uint32_t h = NC_FNV1A_OFFSET;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)data[i];
        h *= NC_FNV1A_PRIME;
    }
    return h;
}

static uint32_t call_hash(const char *tool, const char *input_json) {
    uint32_t h = fnv1a(tool, strlen(tool));
    if (input_json) {
        h ^= fnv1a(input_json, strlen(input_json));
        h *= NC_FNV1A_PRIME;
    }
    return h;
}

void doom_loop_init(void) {
    memset(&g_doom, 0, sizeof(g_doom));
    g_doom.enabled = true;
}

void doom_loop_reset(void) {
    g_doom.head = 0;
    g_doom.count = 0;
    g_doom.fire_count = 0;
}

void doom_loop_set_enabled(bool on) {
    g_doom.enabled = on;
}

bool doom_loop_record(const char *tool_name, const char *input_json) {
    if (!g_doom.enabled || !tool_name)
        return false;

    uint32_t h = call_hash(tool_name, input_json);

    doom_entry_t *slot = &g_doom.ring[g_doom.head % DOOM_LOOP_WINDOW];
    slot->hash = h;
    nc_strlcpy(slot->tool_name, tool_name, sizeof(slot->tool_name));
    g_doom.head++;
    if (g_doom.count < DOOM_LOOP_WINDOW)
        g_doom.count++;

    int matches = 0;
    int start = (g_doom.head > DOOM_LOOP_WINDOW) ? g_doom.head - DOOM_LOOP_WINDOW : 0;
    for (int i = start; i < g_doom.head; i++) {
        if (g_doom.ring[i % DOOM_LOOP_WINDOW].hash == h)
            matches++;
    }

    if (matches >= DOOM_LOOP_THRESHOLD) {
        g_doom.fire_count++;
        return true;
    }
    return false;
}

int doom_loop_fire_count(void) {
    return g_doom.fire_count;
}

const char *doom_loop_last_tool(void) {
    if (g_doom.count == 0)
        return NULL;
    int idx = (g_doom.head - 1) % DOOM_LOOP_WINDOW;
    return g_doom.ring[idx].tool_name;
}

char *doom_loop_format_status(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Doom-loop detector: %s | window=%d/%d | fires=%d",
                   g_doom.enabled ? "active" : "disabled", g_doom.count, DOOM_LOOP_WINDOW,
                   g_doom.fire_count);
    if (g_doom.fire_count > 0 && doom_loop_last_tool())
        strbuf_appendf(&sb, " | last=%s", doom_loop_last_tool());
    return strbuf_detach(&sb);
}

void doom_loop_snapshot(doom_loop_snapshot_t *out) {
    if (!out)
        return;

    memset(out, 0, sizeof(*out));
    out->count = g_doom.count;
    out->fire_count = g_doom.fire_count;
    out->enabled = g_doom.enabled;

    if (out->count < 0)
        out->count = 0;
    if (out->count > DOOM_LOOP_WINDOW)
        out->count = DOOM_LOOP_WINDOW;

    int start = g_doom.head - g_doom.count;
    if (start < 0)
        start = 0;
    for (int i = 0; i < out->count; i++) {
        int idx = (start + i) % DOOM_LOOP_WINDOW;
        out->entries[i].hash = g_doom.ring[idx].hash;
        nc_strlcpy(out->entries[i].tool_name, g_doom.ring[idx].tool_name,
                   sizeof(out->entries[i].tool_name));
    }
}

void doom_loop_restore(const doom_loop_snapshot_t *snap) {
    doom_loop_init();
    if (!snap)
        return;

    g_doom.enabled = snap->enabled;
    g_doom.fire_count = snap->fire_count;
    g_doom.count = snap->count;
    if (g_doom.count < 0)
        g_doom.count = 0;
    if (g_doom.count > DOOM_LOOP_WINDOW)
        g_doom.count = DOOM_LOOP_WINDOW;
    g_doom.head = g_doom.count;

    for (int i = 0; i < g_doom.count; i++) {
        g_doom.ring[i].hash = snap->entries[i].hash;
        nc_strlcpy(g_doom.ring[i].tool_name, snap->entries[i].tool_name,
                   sizeof(g_doom.ring[i].tool_name));
    }
}
