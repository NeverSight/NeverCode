/*
 * key_pool.c — Multi-credential rotation for same-provider failover.
 *
 * Maintains pools of API keys per provider label. When one key hits
 * a rate limit (HTTP 429) or quota error (402), it enters cooldown
 * and the pool rotates to the next available key.
 *
 * Strategies: round_robin, random, least_used, fill_first.
 */

#include "nevercode.h"

/* KPOOL_FAILURE_BACKOFF_MAX_SHIFT in defaults.h */

typedef enum {
    KPOOL_ROUND_ROBIN = 0,
    KPOOL_RANDOM,
    KPOOL_LEAST_USED,
    KPOOL_FILL_FIRST
} kpool_strategy_t;

typedef struct {
    char *key;
    long cooldown_until;
    int use_count;
    int fail_count;
    bool active;
} kpool_entry_t;

typedef struct {
    char label[KPOOL_LABEL_MAX];
    kpool_entry_t entries[KPOOL_MAX_ENTRIES];
    int count;
    int cursor;
    kpool_strategy_t strategy;
} kpool_t;

static struct {
    kpool_t pools[KPOOL_MAX_POOLS];
    int pool_count;
    int total_rotations;
    int total_cooldowns;
} g_kp;

void key_pool_init(void) {
    memset(&g_kp, 0, sizeof(g_kp));
}

void key_pool_free(void) {
    for (int i = 0; i < g_kp.pool_count; i++) {
        kpool_t *p = &g_kp.pools[i];
        for (int j = 0; j < p->count; j++)
            free(p->entries[j].key);
    }
    memset(&g_kp, 0, sizeof(g_kp));
}

static kpool_t *find_pool(const char *label) {
    for (int i = 0; i < g_kp.pool_count; i++)
        if (strcmp(g_kp.pools[i].label, label) == 0)
            return &g_kp.pools[i];
    return NULL;
}

int key_pool_create(const char *label, int strategy) {
    if (!label || g_kp.pool_count >= KPOOL_MAX_POOLS)
        return -1;
    if (find_pool(label))
        return -1;

    kpool_t *p = &g_kp.pools[g_kp.pool_count++];
    memset(p, 0, sizeof(*p));
    nc_strlcpy(p->label, label, sizeof(p->label));
    p->strategy = (strategy >= 0 && strategy <= KPOOL_FILL_FIRST) ? (kpool_strategy_t)strategy
                                                                  : KPOOL_ROUND_ROBIN;
    return 0;
}

int key_pool_add_key(const char *label, const char *key) {
    if (!label || !key)
        return -1;

    kpool_t *p = find_pool(label);
    if (!p) {
        if (key_pool_create(label, KPOOL_ROUND_ROBIN) != 0)
            return -1;
        p = find_pool(label);
    }
    if (!p || p->count >= KPOOL_MAX_ENTRIES)
        return -1;

    kpool_entry_t *e = &p->entries[p->count++];
    e->key = nc_xstrdup(key);
    e->active = true;
    e->cooldown_until = 0;
    e->use_count = 0;
    e->fail_count = 0;
    return 0;
}

static bool entry_available(const kpool_entry_t *e) {
    if (!e->active || !e->key)
        return false;
    if (e->cooldown_until > 0 && nc_time_now() < e->cooldown_until)
        return false;
    return true;
}

static int select_round_robin(kpool_t *p) {
    for (int tries = 0; tries < p->count; tries++) {
        int idx = (p->cursor + tries) % p->count;
        if (entry_available(&p->entries[idx])) {
            p->cursor = (idx + 1) % p->count;
            return idx;
        }
    }
    return -1;
}

static int select_random(kpool_t *p) {
    int avail[KPOOL_MAX_ENTRIES];
    int n = 0;
    for (int i = 0; i < p->count; i++)
        if (entry_available(&p->entries[i]))
            avail[n++] = i;
    if (n == 0)
        return -1;
    return avail[nc_rand_uniform((uint32_t)n)];
}

static int select_least_used(kpool_t *p) {
    int best = -1;
    int best_score = INT32_MAX;
    for (int i = 0; i < p->count; i++) {
        if (!entry_available(&p->entries[i]))
            continue;
        int score =
            p->entries[i].use_count + (p->entries[i].fail_count * KPOOL_LEAST_USED_FAIL_WEIGHT);
        if (score < best_score) {
            best = i;
            best_score = score;
        }
    }
    return best;
}

static int select_fill_first(kpool_t *p) {
    for (int i = 0; i < p->count; i++)
        if (entry_available(&p->entries[i]))
            return i;
    return -1;
}

const char *key_pool_next(const char *label) {
    kpool_t *p = find_pool(label);
    if (!p || p->count == 0)
        return NULL;

    int idx;
    switch (p->strategy) {
    case KPOOL_RANDOM:
        idx = select_random(p);
        break;
    case KPOOL_LEAST_USED:
        idx = select_least_used(p);
        break;
    case KPOOL_FILL_FIRST:
        idx = select_fill_first(p);
        break;
    default:
        idx = select_round_robin(p);
        break;
    }

    if (idx < 0)
        return NULL;

    if (p->entries[idx].fail_count > 0)
        p->entries[idx].fail_count--;
    p->entries[idx].use_count++;
    g_kp.total_rotations++;
    return p->entries[idx].key;
}

void key_pool_report_failure(const char *label, const char *key, int http_status) {
    kpool_t *p = find_pool(label);
    if (!p)
        return;

    for (int i = 0; i < p->count; i++) {
        if (p->entries[i].key && strcmp(p->entries[i].key, key) == 0) {
            p->entries[i].fail_count++;
            long base =
                (http_status == HTTP_RATE_LIMIT) ? KPOOL_COOLDOWN_429 : KPOOL_COOLDOWN_DEFAULT;
            int shift = p->entries[i].fail_count - 1;
            if (shift < 0)
                shift = 0;
            if (shift > KPOOL_FAILURE_BACKOFF_MAX_SHIFT)
                shift = KPOOL_FAILURE_BACKOFF_MAX_SHIFT;
            long cooldown = base << shift;
            p->entries[i].cooldown_until = nc_time_now() + cooldown;
            g_kp.total_cooldowns++;
            break;
        }
    }
}

void key_pool_clear_cooldown(const char *label, const char *key) {
    kpool_t *p = find_pool(label);
    if (!p)
        return;
    for (int i = 0; i < p->count; i++) {
        if (p->entries[i].key && strcmp(p->entries[i].key, key) == 0) {
            p->entries[i].cooldown_until = 0;
            break;
        }
    }
}

int key_pool_available_count(const char *label) {
    kpool_t *p = find_pool(label);
    if (!p)
        return 0;
    int n = 0;
    for (int i = 0; i < p->count; i++)
        if (entry_available(&p->entries[i]))
            n++;
    return n;
}

int key_pool_total_count(const char *label) {
    kpool_t *p = find_pool(label);
    return p ? p->count : 0;
}

int key_pool_pool_count(void) {
    return g_kp.pool_count;
}

int key_pool_total_rotations(void) {
    return g_kp.total_rotations;
}

int key_pool_total_cooldowns(void) {
    return g_kp.total_cooldowns;
}

char *key_pool_format(void) {
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_appendf(&sb, "Key Pools: %d pool(s), %d rotation(s), %d cooldown(s)\n", g_kp.pool_count,
                   g_kp.total_rotations, g_kp.total_cooldowns);

    for (int i = 0; i < g_kp.pool_count; i++) {
        kpool_t *p = &g_kp.pools[i];
        const char *strat = p->strategy == KPOOL_RANDOM       ? "random"
                            : p->strategy == KPOOL_LEAST_USED ? "least_used"
                            : p->strategy == KPOOL_FILL_FIRST ? "fill_first"
                                                              : "round_robin";
        strbuf_appendf(&sb, "  [%s] strategy=%s keys=%d avail=%d\n", p->label, strat, p->count,
                       key_pool_available_count(p->label));
        for (int j = 0; j < p->count; j++) {
            kpool_entry_t *e = &p->entries[j];
            const char *status = entry_available(e)      ? "ok"
                                 : e->cooldown_until > 0 ? "cooldown"
                                                         : "disabled";
            strbuf_appendf(&sb, "    key[%d]: uses=%d fails=%d status=%s\n", j, e->use_count,
                           e->fail_count, status);
        }
    }
    return strbuf_detach(&sb);
}

int key_pool_load_from_config(const cJSON *cfg) {
    if (!cfg)
        return 0;
    const cJSON *pools = cJSON_GetObjectItem(cfg, "key_pools");
    if (!cJSON_IsArray(pools))
        return 0;

    int loaded = 0;
    int n = cJSON_GetArraySize(pools);
    for (int i = 0; i < n && g_kp.pool_count < KPOOL_MAX_POOLS; i++) {
        cJSON *item = cJSON_GetArrayItem(pools, i);
        const char *label = cJSON_GetStringValue(cJSON_GetObjectItem(item, "label"));
        if (!label)
            continue;

        cJSON *strat = cJSON_GetObjectItem(item, "strategy");
        int s = KPOOL_ROUND_ROBIN;
        if (strat && cJSON_IsString(strat)) {
            const char *sv = strat->valuestring;
            if (strcmp(sv, "random") == 0)
                s = KPOOL_RANDOM;
            else if (strcmp(sv, "least_used") == 0)
                s = KPOOL_LEAST_USED;
            else if (strcmp(sv, "fill_first") == 0)
                s = KPOOL_FILL_FIRST;
        }

        key_pool_create(label, s);

        cJSON *keys = cJSON_GetObjectItem(item, "keys");
        if (cJSON_IsArray(keys)) {
            int kn = cJSON_GetArraySize(keys);
            for (int k = 0; k < kn; k++) {
                cJSON *ki = cJSON_GetArrayItem(keys, k);
                if (cJSON_IsString(ki))
                    key_pool_add_key(label, ki->valuestring);
            }
        }
        loaded++;
    }
    return loaded;
}
