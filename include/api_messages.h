/*
 * api_messages.h — Messages-format streaming API backend.
 */

#ifndef API_MESSAGES_H
#define API_MESSAGES_H

#include "nevercode.h"
#include "config.h"

int api_messages_stream(const message_t *messages, int num_messages, const char *system_prompt,
                        const config_t *cfg, const tool_def_t *tools, int num_tools,
                        stream_callback_t callback, void *userdata);

cJSON *messages_build_payload(const message_t *msgs, int n);

#endif
