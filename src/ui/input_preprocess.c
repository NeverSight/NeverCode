/*
 * input_preprocess.c — User input sanitization.
 * Cleans whitespace, strips control characters, validates input length.
 * No content-policy filtering on user text (CTF / research use).
 */

#include "nevercode.h"

/* Limits defined in defaults.h */

static bool has_binary_content(const char *input, size_t len) {
    size_t null_count = 0;
    size_t control_count = 0;
    size_t check_len = len > BINARY_DETECT_SAMPLE ? BINARY_DETECT_SAMPLE : len;

    for (size_t i = 0; i < check_len; i++) {
        unsigned char c = (unsigned char)input[i];
        if (c == 0)
            null_count++;
        else if (c < 32 && c != '\n' && c != '\r' && c != '\t')
            control_count++;
    }

    if (null_count > 0)
        return true;
    if (check_len > 0 && control_count * 100 / check_len > 10)
        return true;
    return false;
}

input_check_t input_check(const char *input) {
    if (!input || *input == '\0')
        return INPUT_EMPTY;

    size_t len = strlen(input);
    if (len > MAX_INPUT_LENGTH)
        return INPUT_TOO_LONG;
    if (has_binary_content(input, len))
        return INPUT_BINARY;

    return INPUT_OK;
}

const char *input_check_name(input_check_t check) {
    switch (check) {
    case INPUT_OK:
        return "ok";
    case INPUT_EMPTY:
        return "empty";
    case INPUT_TOO_LONG:
        return "too_long";
    case INPUT_BINARY:
        return "binary_content";
    }
    return "unknown";
}

bool input_check_is_safe(input_check_t check) {
    return check == INPUT_OK;
}

char *input_strip_control_chars(const char *input) {
    if (!input)
        return NULL;
    size_t len = strlen(input);
    char *out = malloc(len + 1);
    if (!out)
        return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)input[i];
        if (c >= 32 || c == '\n' || c == '\r' || c == '\t') {
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
    return out;
}

char *input_collapse_whitespace(const char *input) {
    if (!input)
        return NULL;
    size_t len = strlen(input);
    char *out = malloc(len + 1);
    if (!out)
        return NULL;

    size_t j = 0;
    int consecutive_newlines = 0;
    bool prev_space = false;

    for (size_t i = 0; i < len; i++) {
        char c = input[i];
        if (c == '\n') {
            consecutive_newlines++;
            if (consecutive_newlines <= MAX_CONSECUTIVE_NEWLINES) {
                out[j++] = c;
            }
            prev_space = false;
        } else if (c == ' ' || c == '\t') {
            consecutive_newlines = 0;
            if (!prev_space) {
                out[j++] = ' ';
                prev_space = true;
            }
        } else {
            consecutive_newlines = 0;
            prev_space = false;
            out[j++] = c;
        }
    }
    out[j] = '\0';
    return out;
}

char *input_trim(const char *input) {
    if (!input)
        return NULL;
    size_t len = strlen(input);
    if (len == 0)
        return nc_xstrdup("");

    size_t start = 0;
    while (start < len && (input[start] == ' ' || input[start] == '\t' || input[start] == '\n' ||
                           input[start] == '\r'))
        start++;

    size_t end = len;
    while (end > start && (input[end - 1] == ' ' || input[end - 1] == '\t' ||
                           input[end - 1] == '\n' || input[end - 1] == '\r'))
        end--;

    size_t rlen = end - start;
    char *out = malloc(rlen + 1);
    if (!out)
        return NULL;
    memcpy(out, input + start, rlen);
    out[rlen] = '\0';
    return out;
}

char *input_preprocess(const char *raw_input, input_check_t *check_out) {
    input_check_t check = input_check(raw_input);
    if (check_out)
        *check_out = check;

    if (check == INPUT_EMPTY || check == INPUT_TOO_LONG || check == INPUT_BINARY)
        return NULL;

    char *stripped = input_strip_control_chars(raw_input);
    if (!stripped)
        return NULL;

    char *trimmed = input_trim(stripped);
    free(stripped);
    if (!trimmed)
        return NULL;

    if (trimmed[0] == '\0') {
        free(trimmed);
        if (check_out)
            *check_out = INPUT_EMPTY;
        return NULL;
    }

    char *collapsed = input_collapse_whitespace(trimmed);
    free(trimmed);
    if (!collapsed)
        return NULL;

    if (text_has_surrogates(collapsed, strlen(collapsed))) {
        char *clean = text_strip_surrogates(collapsed, strlen(collapsed));
        free(collapsed);
        collapsed = clean;
    }

    return collapsed;
}

bool input_is_warn_length(const char *input) {
    if (!input)
        return false;
    return strlen(input) > WARN_INPUT_LENGTH;
}

size_t input_max_length(void) {
    return MAX_INPUT_LENGTH;
}
