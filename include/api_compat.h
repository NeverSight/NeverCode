/*
 * api_compat.h — Chat-completions compatible streaming API.
 *
 * Implements the /v1/chat/completions SSE streaming protocol used by
 * most LLM providers.
 */

#ifndef API_COMPAT_H
#define API_COMPAT_H

#include "nevercode.h"
#include "config.h"

int api_compat_stream(const message_t *messages, int num_messages, const char *system_prompt,
                      const config_t *cfg, const tool_def_t *tools, int num_tools,
                      stream_callback_t callback, void *userdata);

cJSON *compat_build_messages(const message_t *msgs, int n, const char *sys);
void compat_set_developer_role(bool enabled);

#endif
