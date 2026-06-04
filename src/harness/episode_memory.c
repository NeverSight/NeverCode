/*
 * episode_memory.c — Episodic experience recall for recurring patterns.
 *
 * Records symptom/diagnosis/resolution triples from past debugging sessions.
 * On lookup, matches symptoms by word overlap and returns ranked prior
 * episodes so the agent can learn from previous encounters.
 * Persists to ~/.agents/episodes.json for cross-session learning.
 */

#include "nevercode.h"
#include "defaults.h"

typedef enum { EP_SUCCESS = 0, EP_PARTIAL = 1, EP_FAILURE = 2 } ep_outcome_t;

static const char *ep_outcome_names[] = {"success", "partial", "failure"};

typedef struct {
    char symptom[EPISODE_DESC_MAX];
    char diagnosis[EPISODE_DESC_MAX];
    char resolution[EPISODE_DESC_MAX];
    char tags[EPISODE_TAG_BUF];
    ep_outcome_t outcome;
    int turns_spent;
    long timestamp;
    int reuse_count;
} episode_t;

static struct {
    episode_t entries[EPISODE_MAX_ENTRIES];
    int count;
    int total_lookups;
    int total_hits;
    bool init;
    char persist_path[NC_PATH_MAX];
} g_ep;

/* ── Init / Free ── */

void episode_memory_init(void) {
    memset(&g_ep, 0, sizeof(g_ep));
    g_ep.init = true;
    config_home_path(g_ep.persist_path, sizeof(g_ep.persist_path), EPISODE_PERSIST_FILE);
}

void episode_memory_free(void) {
    memset(&g_ep, 0, sizeof(g_ep));
}

/* ── Record ── */

int episode_memory_record(const char *symptom, const char *diagnosis, const char *resolution,
                          int outcome, int turns_spent) {
    if (!g_ep.init || !symptom || g_ep.count >= EPISODE_MAX_ENTRIES)
        return -1;

    episode_t *e = &g_ep.entries[g_ep.count];
    memset(e, 0, sizeof(*e));
    nc_strlcpy(e->symptom, symptom, sizeof(e->symptom));
    if (diagnosis)
        nc_strlcpy(e->diagnosis, diagnosis, sizeof(e->diagnosis));
    if (resolution)
        nc_strlcpy(e->resolution, resolution, sizeof(e->resolution));
    e->outcome = (ep_outcome_t)outcome;
    e->turns_spent = turns_spent;
    e->timestamp = nc_wall_sec();
    g_ep.count++;
    return g_ep.count - 1;
}

int episode_memory_tag(int idx, const char *tag) {
    if (!g_ep.init || idx < 0 || idx >= g_ep.count || !tag)
        return -1;

    episode_t *e = &g_ep.entries[idx];
    size_t cur = strlen(e->tags);
    size_t tlen = strlen(tag);
    if (cur + tlen + 2 >= sizeof(e->tags))
        return -1;

    if (cur > 0) {
        e->tags[cur] = ',';
        cur++;
    }
    memcpy(e->tags + cur, tag, tlen);
    e->tags[cur + tlen] = '\0';
    return 0;
}

/* ── Lookup by word-match scoring ── */

static int ep_word_match(const char *a, const char *b) {
    if (!a || !b)
        return 0;
    int score = 0;
    const char *p = a;
    while (*p) {
        char w[NC_TAG_MAX];
        int wi = 0;
        while (*p && *p != ' ' && *p != ',' && wi < 126)
            w[wi++] = *p++;
        w[wi] = '\0';
        while (*p == ' ' || *p == ',')
            p++;
        if (wi >= 3 && strcasestr(b, w))
            score += wi;
    }
    return score;
}

int episode_memory_lookup(const char *symptom, int *out_indices, int max_results) {
    if (!g_ep.init || !symptom || !out_indices || max_results <= 0)
        return 0;

    g_ep.total_lookups++;

    typedef struct {
        int idx;
        int score;
    } ranked_t;
    ranked_t ranked[EPISODE_MAX_ENTRIES];
    int rcount = 0;

    for (int i = 0; i < g_ep.count; i++) {
        int s = ep_word_match(symptom, g_ep.entries[i].symptom) +
                ep_word_match(symptom, g_ep.entries[i].tags);
        if (s > 0) {
            ranked[rcount].idx = i;
            ranked[rcount].score = s;
            rcount++;
        }
    }

    for (int i = 0; i < rcount - 1; i++)
        for (int j = i + 1; j < rcount; j++)
            if (ranked[j].score > ranked[i].score) {
                ranked_t t = ranked[i];
                ranked[i] = ranked[j];
                ranked[j] = t;
            }

    int n = NC_MIN(rcount, max_results);
    for (int i = 0; i < n; i++) {
        out_indices[i] = ranked[i].idx;
        g_ep.entries[ranked[i].idx].reuse_count++;
    }
    if (n > 0)
        g_ep.total_hits++;
    return n;
}

/* ── Context building ── */

char *episode_memory_build_prompt(const char *symptom) {
    if (!g_ep.init)
        return nc_xstrdup("");

    int indices[EPISODE_PROMPT_MAX];
    int n = episode_memory_lookup(symptom, indices, EPISODE_PROMPT_MAX);
    if (n == 0)
        return nc_xstrdup("");

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "<prior_episodes count=\"%d\">\n", n);

    for (int i = 0; i < n; i++) {
        episode_t *e = &g_ep.entries[indices[i]];
        int oi = (int)e->outcome;
        if (oi < 0 || oi > 2)
            oi = 2;
        strbuf_appendf(&sb,
                       "  <episode outcome=\"%s\" turns=\"%d\" reuses=\"%d\">\n"
                       "    <symptom>%s</symptom>\n"
                       "    <diagnosis>%s</diagnosis>\n"
                       "    <resolution>%s</resolution>\n"
                       "  </episode>\n",
                       ep_outcome_names[oi], e->turns_spent, e->reuse_count, e->symptom,
                       e->diagnosis, e->resolution);
    }
    strbuf_appendf(&sb, "</prior_episodes>\n");
    return strbuf_detach(&sb);
}

/* ── Queries ── */

int episode_memory_count(void) {
    return g_ep.count;
}
int episode_memory_lookup_count(void) {
    return g_ep.total_lookups;
}
int episode_memory_hit_count(void) {
    return g_ep.total_hits;
}

/* ── Persistence ── */

int episode_memory_save(void) {
    if (!g_ep.init || g_ep.persist_path[0] == '\0' || g_ep.count == 0)
        return -1;

    if (nc_mkdir_p(config_home_dir()) != 0)
        return -1;

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < g_ep.count; i++) {
        episode_t *e = &g_ep.entries[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "symptom", e->symptom);
        cJSON_AddStringToObject(obj, "diagnosis", e->diagnosis);
        cJSON_AddStringToObject(obj, "resolution", e->resolution);
        cJSON_AddStringToObject(obj, "tags", e->tags);
        cJSON_AddNumberToObject(obj, "outcome", (int)e->outcome);
        cJSON_AddNumberToObject(obj, "turns", e->turns_spent);
        cJSON_AddNumberToObject(obj, "ts", e->timestamp);
        cJSON_AddNumberToObject(obj, "reuses", e->reuse_count);
        cJSON_AddItemToArray(arr, obj);
    }

    char *json = nc_cjson_detach(arr);
    if (!json)
        return -1;

    int rc = nc_write_file_atomic(g_ep.persist_path, json, strlen(json));
    free(json);
    return rc;
}

int episode_memory_load(void) {
    if (!g_ep.init || g_ep.persist_path[0] == '\0')
        return -1;

    size_t flen = 0;
    char *raw = strbuf_read_file(g_ep.persist_path, &flen);
    if (!raw)
        return -1;

    cJSON *arr = cJSON_Parse(raw);
    free(raw);
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        return -1;
    }

    g_ep.count = 0;
    int loaded = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        if (g_ep.count >= EPISODE_MAX_ENTRIES)
            break;

        episode_t *e = &g_ep.entries[g_ep.count];
        memset(e, 0, sizeof(*e));

        cJSON *v;
        if ((v = cJSON_GetObjectItem(item, "symptom")) && cJSON_IsString(v))
            nc_strlcpy(e->symptom, v->valuestring, sizeof(e->symptom));
        if ((v = cJSON_GetObjectItem(item, "diagnosis")) && cJSON_IsString(v))
            nc_strlcpy(e->diagnosis, v->valuestring, sizeof(e->diagnosis));
        if ((v = cJSON_GetObjectItem(item, "resolution")) && cJSON_IsString(v))
            nc_strlcpy(e->resolution, v->valuestring, sizeof(e->resolution));
        if ((v = cJSON_GetObjectItem(item, "tags")) && cJSON_IsString(v))
            nc_strlcpy(e->tags, v->valuestring, sizeof(e->tags));
        if ((v = cJSON_GetObjectItem(item, "outcome")) && cJSON_IsNumber(v))
            e->outcome = (ep_outcome_t)v->valueint;
        if ((v = cJSON_GetObjectItem(item, "turns")) && cJSON_IsNumber(v))
            e->turns_spent = v->valueint;
        if ((v = cJSON_GetObjectItem(item, "ts")) && cJSON_IsNumber(v))
            e->timestamp = (long)v->valuedouble;
        if ((v = cJSON_GetObjectItem(item, "reuses")) && cJSON_IsNumber(v))
            e->reuse_count = v->valueint;

        g_ep.count++;
        loaded++;
    }
    cJSON_Delete(arr);
    return loaded;
}

/* ── Formatting ── */

char *episode_memory_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Episode Memory: %d entries, %d lookups, %d hits\n", g_ep.count,
                   g_ep.total_lookups, g_ep.total_hits);

    for (int i = 0; i < g_ep.count; i++) {
        episode_t *e = &g_ep.entries[i];
        int oi = (int)e->outcome;
        if (oi < 0 || oi > 2)
            oi = 2;
        strbuf_appendf(&sb, "  [%d] (%s, %d turns, reused %dx) %s\n", i, ep_outcome_names[oi],
                       e->turns_spent, e->reuse_count, e->symptom);
    }
    return strbuf_detach(&sb);
}
