/*
 * round_grouping.c — API round counting and analysis.
 *
 * Thin wrapper over compact_group_by_api_round() providing a
 * high-level round count without exposing the msg_group_t details.
 */
#include "nevercode.h"
#include "conversation.h"
#include "defaults.h"

int count_api_rounds(const void *msgs_ptr, int len) {
    const message_t *msgs = (const message_t *)msgs_ptr;
    msg_group_t tmp[CTX_MAX_MSG_GROUPS];
    return compact_group_by_api_round(msgs, len, tmp, CTX_MAX_MSG_GROUPS);
}
