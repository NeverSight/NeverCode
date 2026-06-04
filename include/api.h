#ifndef API_H
#define API_H

#include "nevercode.h"
#include "config.h"

int api_stream_request(const message_t *messages, int num_messages, const char *system_prompt,
                       const config_t *cfg, const tool_def_t *tools, int num_tools,
                       stream_callback_t callback, void *userdata);

/* Record model/tools fingerprint and bump turn counter for prompt-cache locality.
 * Call once per logical assistant turn (e.g. REPL), not for internal helper requests. */
void api_stream_record_cache(const config_t *cfg, const tool_def_t *tools, int num_tools);

/* Simple one-shot API call: collect full response into strbuf */
int api_stream_simple(const char *base_url, const char *api_key, const char *model,
                      provider_type_t provider, const char *system_prompt,
                      const message_t *messages, int num_messages, strbuf_t *response_out,
                      int *in_tokens_out, int *out_tokens_out);

/* Multi-provider fallback streaming */
int multi_provider_stream_with_fallback(const message_t *msgs, int n, const char *sys,
                                        const config_t *cfg, const tool_def_t *tools, int num_tools,
                                        stream_callback_t callback, void *userdata);

/* Provider info registry */
void pinfo_init(void);
int pinfo_register(const char *id, const char *name, const char *base_url, const char *env_key);
int pinfo_add_header(const char *provider_id, const char *key, const char *value);
int pinfo_set_retry(const char *provider_id, int stream_retries, int request_retries,
                    int idle_timeout_ms);
int pinfo_set_caps(const char *provider_id, bool vision, bool func_call, bool streaming,
                   bool reasoning);
const char *pinfo_base_url(const char *provider_id);
const char *pinfo_env_key(const char *provider_id);
bool pinfo_has_vision(const char *provider_id);
bool pinfo_has_reasoning(const char *provider_id);
int pinfo_stream_retries(const char *provider_id);
int pinfo_idle_timeout_ms(const char *provider_id);
int pinfo_count(void);
char *pinfo_format_list(void);
const char *pinfo_detect_provider_id(const char *url);
int pinfo_auto_register_from_url(const char *url);

#endif /* API_H */
