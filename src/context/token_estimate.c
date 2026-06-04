/*
 * token_estimate.c — Heuristic BPE token counter.
 *
 * Approximates token counts without a real tokenizer by combining
 * structural analysis (word/punct/multi-byte splitting) with a
 * bytes-per-token ratio estimate, then averaging the two.
 *
 * CJK ideographs (U+3000..U+9FFF, U+F900..U+FAFF) are weighted at
 * TOEST_CJK_WEIGHT tokens each because BPE tokenizers typically need
 * 2-3 sub-tokens per ideograph.  Four-byte sequences (emoji, rare
 * scripts) use TOEST_4BYTE_WEIGHT.
 */

#include "nevercode.h"

static int utf8_token_weight(const unsigned char *p, size_t remaining, size_t *advance) {
    unsigned char c = p[0];
    if (c < 0xC0) {
        *advance = 1;
        return 1;
    }
    if (c < 0xE0) {
        *advance = (remaining >= 2) ? 2 : 1;
        return 1;
    }
    if (c < 0xF0) {
        *advance = (remaining >= 3) ? 3 : 1;
        if (remaining >= 3) {
            uint32_t cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) |
                          (uint32_t)(p[2] & 0x3F);
            if ((cp >= 0x3000 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF))
                return TOEST_CJK_WEIGHT;
        }
        return TOEST_3BYTE_WEIGHT;
    }
    *advance = (remaining >= 4) ? 4 : 1;
    return TOEST_4BYTE_WEIGHT;
}

int estimate_tokens(const char *text) {
    if (!text || !text[0])
        return 0;
    size_t len = strlen(text);
    int tokens = 0;
    bool in_word = false;

    for (size_t i = 0; i < len;) {
        unsigned char c = (unsigned char)text[i];
        if (c <= 0x20 || c == '\n' || c == '\t') {
            if (in_word) {
                tokens++;
                in_word = false;
            }
            i++;
        } else if ((c >= '!' && c <= '/') || (c >= ':' && c <= '@') || (c >= '[' && c <= '`') ||
                   (c >= '{' && c <= '~')) {
            if (in_word) {
                tokens++;
                in_word = false;
            }
            tokens++;
            i++;
        } else if (c >= 0x80) {
            if (in_word) {
                tokens++;
                in_word = false;
            }
            size_t adv = 1;
            tokens += utf8_token_weight((const unsigned char *)text + i, len - i, &adv);
            i += adv;
        } else {
            in_word = true;
            i++;
        }
    }
    if (in_word)
        tokens++;

    int ratio_estimate = (int)(len * TOEST_RATIO_NUM / TOEST_RATIO_DEN);
    return (tokens + ratio_estimate) / 2;
}

int estimate_message_tokens(const message_t *m) {
    int total = 4;
    for (int i = 0; i < m->num_blocks; i++) {
        const content_block_t *b = &m->blocks[i];
        if (b->text)
            total += estimate_tokens(b->text);
        if (b->tool_name)
            total += estimate_tokens(b->tool_name) + 5;
        if (b->tool_input) {
            char *json = cJSON_PrintUnformatted(b->tool_input);
            if (json) {
                total += estimate_tokens(json);
                free(json);
            }
        }
    }
    return total;
}

int estimate_conversation_tokens(const message_t *msgs, int n, const char *sys) {
    int total = 0;
    if (sys)
        total += estimate_tokens(sys) + 4;
    for (int i = 0; i < n; i++)
        total += estimate_message_tokens(&msgs[i]);
    return total;
}
