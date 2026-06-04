/*
 * rollout_truncate.c — Pruning old rollout events to bound memory usage
 *
 * When the rollout event log grows past ROLLOUT_TRUNCATE_THRESHOLD,
 * drops older events while preserving the most recent N user-turn
 * boundaries (where N = ROLLOUT_TRUNCATE_KEEP_TURNS).
 *
 * Also trims the on-disk events.jsonl by rewriting only surviving events.
 */

#include "nevercode.h"
#include "defaults.h"

/* ── Event classification ── */

static bool is_user_turn(const char *type_str) {
    return type_str && strcmp(type_str, "user") == 0;
}

static bool is_checkpoint(const char *type_str) {
    if (!type_str)
        return false;
    return strcmp(type_str, "compact") == 0 || strcmp(type_str, "checkpoint") == 0 ||
           strcmp(type_str, "handoff") == 0;
}

/* ── In-memory rollout truncation ── */

typedef struct {
    int position;
    int turn_id;
} turn_boundary_t;

int rollout_truncate_find_user_boundaries(const cJSON *events_arr, int *positions, int max_pos) {
    if (!events_arr || !cJSON_IsArray(events_arr) || !positions)
        return 0;

    int count = 0;
    int idx = 0;
    const cJSON *ev;
    cJSON_ArrayForEach(ev, events_arr) {
        const cJSON *type = cJSON_GetObjectItem(ev, "type");
        if (type && cJSON_IsString(type) && is_user_turn(type->valuestring)) {
            if (count < max_pos)
                positions[count] = idx;
            count++;
        }
        idx++;
    }
    return count;
}

int rollout_truncate_find_cut_point(const cJSON *events_arr, int keep_turns) {
    if (!events_arr || !cJSON_IsArray(events_arr) || keep_turns <= 0)
        return 0;

    int total = cJSON_GetArraySize(events_arr);
    if (total <= ROLLOUT_TRUNCATE_THRESHOLD)
        return 0;

    int positions[ROLLOUT_MAX_EVENTS];
    int n_boundaries =
        rollout_truncate_find_user_boundaries(events_arr, positions, ROLLOUT_MAX_EVENTS);

    if (n_boundaries <= keep_turns)
        return 0;

    int cut_boundary = n_boundaries - keep_turns;
    int cut_idx = positions[cut_boundary - 1];

    /* Walk back to include any preceding checkpoint */
    for (int i = cut_idx - 1; i >= 0; i--) {
        cJSON *ev = cJSON_GetArrayItem(events_arr, i);
        const cJSON *type = cJSON_GetObjectItem(ev, "type");
        if (type && cJSON_IsString(type) && is_checkpoint(type->valuestring)) {
            cut_idx = i;
            break;
        }
    }

    return cut_idx;
}

cJSON *rollout_truncate_events(const cJSON *events_arr, int keep_turns) {
    if (!events_arr || !cJSON_IsArray(events_arr))
        return NULL;

    int cut = rollout_truncate_find_cut_point(events_arr, keep_turns);
    if (cut <= 0)
        return cJSON_Duplicate(events_arr, 1);

    int total = cJSON_GetArraySize(events_arr);
    cJSON *result = cJSON_CreateArray();

    /* Insert a synthetic compaction marker */
    cJSON *marker = cJSON_CreateObject();
    cJSON_AddStringToObject(marker, "type", "compact");
    cJSON_AddNumberToObject(marker, "turn", 0);
    cJSON_AddNumberToObject(marker, "truncated_events", cut);
    cJSON_AddItemToArray(result, marker);

    for (int i = cut; i < total; i++) {
        cJSON *ev = cJSON_GetArrayItem(events_arr, i);
        cJSON_AddItemToArray(result, cJSON_Duplicate(ev, 1));
    }

    return result;
}

/* ── On-disk JSONL truncation ── */

int rollout_truncate_file(const char *session_id, int keep_turns) {
    if (!session_id || keep_turns <= 0)
        return -1;

    char dir[NC_PATH_MAX];
    config_home_path(dir, sizeof(dir), "sessions");
    char events_path[NC_PATH_MAX];
    snprintf(events_path, sizeof(events_path), "%s/%s/events.jsonl", dir, session_id);

    FILE *f = fopen(events_path, "r");
    if (!f)
        return -1;

    cJSON *all = cJSON_CreateArray();
    char line[REPLAY_LINE_BUF];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = '\0';
        if (len == 0)
            continue;
        cJSON *ev = cJSON_Parse(line);
        if (ev)
            cJSON_AddItemToArray(all, ev);
    }
    fclose(f);

    int total = cJSON_GetArraySize(all);
    if (total <= ROLLOUT_TRUNCATE_THRESHOLD) {
        cJSON_Delete(all);
        return 0;
    }

    cJSON *trimmed = rollout_truncate_events(all, keep_turns);
    cJSON_Delete(all);
    if (!trimmed)
        return -1;

    /* Atomic rewrite: write to .tmp then rename */
    char tmp_path[NC_PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", events_path);
    f = fopen(tmp_path, "w");
    if (!f) {
        cJSON_Delete(trimmed);
        return -1;
    }

    int n = cJSON_GetArraySize(trimmed);
    for (int i = 0; i < n; i++) {
        cJSON *ev = cJSON_GetArrayItem(trimmed, i);
        char *j = cJSON_PrintUnformatted(ev);
        if (j) {
            fprintf(f, "%s\n", j);
            free(j);
        }
    }
    int flush_err = fflush(f);
    fclose(f);
    cJSON_Delete(trimmed);

    if (flush_err || rename(tmp_path, events_path) != 0) {
        unlink(tmp_path);
        return -1;
    }

    return total - n;
}

int rollout_truncate_check_needed(const char *session_id) {
    if (!session_id)
        return 0;
    char dir[NC_PATH_MAX];
    config_home_path(dir, sizeof(dir), "sessions");
    char events_path[NC_PATH_MAX];
    snprintf(events_path, sizeof(events_path), "%s/%s/events.jsonl", dir, session_id);

    FILE *f = fopen(events_path, "r");
    if (!f)
        return 0;
    int count = 0;
    char line[NC_LABEL_MAX];
    while (fgets(line, sizeof(line), f))
        if (line[0] != '\n')
            count++;
    fclose(f);
    return count > ROLLOUT_TRUNCATE_THRESHOLD ? 1 : 0;
}
