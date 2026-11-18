/*
 * pinned_msg.c — Message pinning for compaction resistance.
 *
 * Allows marking conversation messages as "pinned" so they survive
 * context compaction.  Pinned messages are never dropped by L2/L3/L4
 * compaction levels and are always preserved in the tail.
 *
 * Pin indices are stored per session and can be persisted to disk.
 */

#include "nevercode.h"

#define PIN_MAX NC_PINNED_MAX

static struct {
    int indices[PIN_MAX];
    int count;
    char session_id[NC_TAG_MAX];
} g_pins;

void pinned_msg_init(void) {
    memset(&g_pins, 0, sizeof(g_pins));
}

void pinned_msg_set_session(const char *sid) {
    if (sid)
        nc_strlcpy(g_pins.session_id, sid, sizeof(g_pins.session_id));
}

bool pinned_msg_pin(int idx) {
    if (idx < 0 || g_pins.count >= PIN_MAX)
        return false;
    for (int i = 0; i < g_pins.count; i++) {
        if (g_pins.indices[i] == idx)
            return true;
    }
    g_pins.indices[g_pins.count++] = idx;
    return true;
}

bool pinned_msg_unpin(int idx) {
    for (int i = 0; i < g_pins.count; i++) {
        if (g_pins.indices[i] == idx) {
            memmove(&g_pins.indices[i], &g_pins.indices[i + 1],
                    (size_t)(g_pins.count - i - 1) * sizeof(int));
            g_pins.count--;
            return true;
        }
    }
    return false;
}

bool pinned_msg_toggle(int idx) {
    if (pinned_msg_is_pinned(idx)) {
        pinned_msg_unpin(idx);
        return false;
    }
    pinned_msg_pin(idx);
    return true;
}

bool pinned_msg_is_pinned(int idx) {
    for (int i = 0; i < g_pins.count; i++) {
        if (g_pins.indices[i] == idx)
            return true;
    }
    return false;
}

int pinned_msg_count(void) {
    return g_pins.count;
}

void pinned_msg_clear(void) {
    g_pins.count = 0;
}

void pinned_msg_shift(int removed_before, int n_removed) {
    if (n_removed <= 0)
        return;
    int out = 0;
    for (int i = 0; i < g_pins.count; i++) {
        if (g_pins.indices[i] < removed_before) {
            g_pins.indices[out++] = g_pins.indices[i];
        } else if (g_pins.indices[i] >= removed_before + n_removed) {
            g_pins.indices[out++] = g_pins.indices[i] - n_removed;
        }
    }
    g_pins.count = out;
}

int pinned_msg_lowest_pinned(void) {
    if (g_pins.count == 0)
        return -1;
    int low = g_pins.indices[0];
    for (int i = 1; i < g_pins.count; i++) {
        if (g_pins.indices[i] < low)
            low = g_pins.indices[i];
    }
    return low;
}

char *pinned_msg_format(int conv_len) {
    if (g_pins.count == 0)
        return nc_xstrdup("No pinned messages.\n");

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Pinned messages: %d\n", g_pins.count);
    for (int i = 0; i < g_pins.count; i++) {
        int idx = g_pins.indices[i];
        strbuf_appendf(&sb, "  [%d]%s\n", idx, idx >= conv_len ? " (out of range)" : "");
    }
    return strbuf_detach(&sb);
}

int pinned_msg_save(void) {
    if (!g_pins.session_id[0])
        return -1;

    char path[NC_PATH_MAX];
    config_home_path(path, sizeof(path), "/sessions/");
    nc_mkdir_p(path);

    char full[NC_PATH_MAX];
    snprintf(full, sizeof(full), "%.2040s%.2040s.pins.json", path, g_pins.session_id);

    cJSON *arr = cJSON_CreateIntArray(g_pins.indices, g_pins.count);
    if (!arr)
        return -1;

    int rc = nc_json_save(full, arr);
    cJSON_Delete(arr);
    return rc;
}

int pinned_msg_load(const char *sid) {
    if (!sid)
        return -1;

    pinned_msg_init();
    pinned_msg_set_session(sid);

    char path[NC_PATH_MAX];
    config_home_path(path, sizeof(path), "/sessions/");

    char full[NC_PATH_MAX];
    snprintf(full, sizeof(full), "%.2040s%.2040s.pins.json", path, sid);

    char *buf = strbuf_read_file_max(full, NULL, NC_READ_CAP_JSON_SMALL);
    if (!buf)
        return 0;

    cJSON *arr = cJSON_Parse(buf);
    free(buf);
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        return 0;
    }

    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && g_pins.count < PIN_MAX; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (cJSON_IsNumber(item))
            g_pins.indices[g_pins.count++] = item->valueint;
    }
    cJSON_Delete(arr);
    return g_pins.count;
}
