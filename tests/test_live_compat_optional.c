/*
 * test_live_compat_optional.c — optional HTTP check against a user-provided gateway.
 *
 * Skipped unless NEVERCODE_LIVE_COMPAT=1. Requires OPENAI_BASE_URL and OPENAI_API_KEY.
 */

#include "test.h"
#include "nevercode.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t len;
} live_mem_t;

static size_t live_write_cb(char *ptr, size_t sz, size_t nmemb, void *userdata) {
    live_mem_t *m = (live_mem_t *)userdata;
    size_t add = sz * nmemb;
    char *n = realloc(m->data, m->len + add + 1);
    if (!n)
        return 0;
    m->data = n;
    memcpy(m->data + m->len, ptr, add);
    m->len += add;
    m->data[m->len] = '\0';
    return add;
}

static void live_build_chat_url(const char *base, char *out, size_t osz) {
    size_t n = strlen(base);
    while (n > 0 && base[n - 1] == '/')
        n--;
    if (n >= 3 && strncmp(base + n - 3, "/v1", 3) == 0)
        snprintf(out, osz, "%.*s/chat/completions", (int)n, base);
    else
        snprintf(out, osz, "%.*s/v1/chat/completions", (int)n, base);
}

static int live_compat_skipped(void) {
    const char *v = getenv(ENV_LIVE_COMPAT);
    return !v || strcmp(v, "1") != 0;
}

static int test_live_chat_completions_ok(void) {
    if (live_compat_skipped()) {
        printf("  (skip live compat: set " ENV_LIVE_COMPAT "=1 to enable)\n");
        return 0;
    }

    const char *base = getenv(ENV_COMPAT_URL);
    const char *key = getenv(ENV_COMPAT_KEY);
    if (!base || !key || !base[0] || !key[0]) {
        fprintf(stderr,
                "  FAIL: " ENV_COMPAT_URL " and " ENV_COMPAT_KEY " required for live test\n");
        return 1;
    }

    const char *model = getenv(ENV_LIVE_MODEL);
    if (!model || !model[0])
        model = nc_provider_default_model(PROVIDER_COMPAT);
    const char *prompt = getenv(ENV_LIVE_PROMPT);
    if (!prompt || !prompt[0])
        prompt = NC_SMOKE_ONESHOT_PROMPT;

    char url[NC_URL_MAX];
    live_build_chat_url(base, url, sizeof(url));

    cJSON *root = cJSON_CreateObject();
    ASSERT_NOT_NULL(root);
    cJSON_AddStringToObject(root, "model", model);
    cJSON *msgs = cJSON_CreateArray();
    cJSON *um = cJSON_CreateObject();
    cJSON_AddStringToObject(um, "role", "user");
    cJSON_AddStringToObject(um, "content", prompt);
    cJSON_AddItemToArray(msgs, um);
    cJSON_AddItemToObject(root, "messages", msgs);
    cJSON_AddNumberToObject(root, "max_tokens", 32);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ASSERT_NOT_NULL(body);

    size_t auth_len = strlen(key) + 64;
    char *auth_line = malloc(auth_len);
    ASSERT_NOT_NULL(auth_line);
    snprintf(auth_line, auth_len, "Authorization: Bearer %s", key);

    struct curl_slist *hdrs = curl_slist_append(NULL, "Content-Type: application/json");
    hdrs = curl_slist_append(hdrs, auth_line);

    live_mem_t mem = {0};
    CURL *curl = curl_easy_init();
    ASSERT_NOT_NULL(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, live_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mem);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)NC_LIVE_COMPAT_HTTP_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)NC_LIVE_COMPAT_CONNECT_TIMEOUT_S);

    CURLcode cr = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    curl_slist_free_all(hdrs);
    free(auth_line);
    free(body);

    if (cr != CURLE_OK) {
        fprintf(stderr, "  FAIL: curl: %s\n", curl_easy_strerror(cr));
        free(mem.data);
        return 1;
    }
    if (http != 200L) {
        fprintf(stderr, "  FAIL: HTTP %ld body: %s\n", http, mem.data ? mem.data : "");
        free(mem.data);
        return 1;
    }
    ASSERT_NOT_NULL(mem.data);
    ASSERT_STR_CONTAINS(mem.data, "choices");
    free(mem.data);
    return 0;
}

int test_live_compat_optional_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("live_compat_optional");
    RUN_TEST(test_live_chat_completions_ok);
    TEST_RETURN_FAILED();
}
