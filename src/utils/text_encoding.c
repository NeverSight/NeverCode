/*
 * text_encoding.c — Text encoding detection and binary file protection
 *
 * Detects file encoding, identifies binary content, and ensures safe
 * handling of non-UTF-8 data.
 */

#include "nevercode.h"

typedef enum { ENC_UTF8, ENC_ASCII, ENC_LATIN1, ENC_BINARY, ENC_UNKNOWN } text_encoding_t;

static const char *encoding_name(text_encoding_t enc) {
    switch (enc) {
    case ENC_UTF8:
        return "utf-8";
    case ENC_ASCII:
        return "ascii";
    case ENC_LATIN1:
        return "latin-1";
    case ENC_BINARY:
        return "binary";
    default:
        return "unknown";
    }
}

text_encoding_t text_detect_encoding(const unsigned char *data, size_t len) {
    if (!data || len == 0)
        return ENC_ASCII;

    bool has_high = false;
    bool valid_utf8 = true;
    int null_count = 0;
    int ctrl_count = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = data[i];

        if (c == 0) {
            null_count++;
            if (null_count > 2)
                return ENC_BINARY;
            continue;
        }

        if (c < 0x20 && c != '\n' && c != '\r' && c != '\t' && c != 0x1b) {
            ctrl_count++;
        }

        if (c >= 0x80) {
            has_high = true;
            if (valid_utf8) {
                int expected = 0;
                if ((c & 0xE0) == 0xC0)
                    expected = 1;
                else if ((c & 0xF0) == 0xE0)
                    expected = 2;
                else if ((c & 0xF8) == 0xF0)
                    expected = 3;
                else {
                    valid_utf8 = false;
                    continue;
                }

                for (int j = 0; j < expected && i + 1 < len; j++) {
                    i++;
                    if ((data[i] & 0xC0) != 0x80) {
                        valid_utf8 = false;
                        break;
                    }
                }
            }
        }
    }

    double ctrl_ratio = (double)ctrl_count / (double)len;
    if (ctrl_ratio > 0.1)
        return ENC_BINARY;

    if (!has_high)
        return ENC_ASCII;
    if (valid_utf8)
        return ENC_UTF8;
    return ENC_LATIN1;
}

bool text_is_binary_data(const unsigned char *data, size_t len) {
    return text_detect_encoding(data, len) == ENC_BINARY;
}

bool text_is_binary_file(const char *path) {
    if (!path)
        return false;

    const char *ext = strrchr(path, '.');
    if (ext) {
        ext++;
        static const char *binary_exts[] = {
            "png",   "jpg",  "jpeg",   "gif", "bmp",     "ico",   "webp", "svg", "mp3",
            "mp4",   "avi",  "mkv",    "mov", "wav",     "flac",  "ogg",  "zip", "tar",
            "gz",    "bz2",  "xz",     "7z",  "rar",     "zst",   "exe",  "dll", "so",
            "dylib", "a",    "o",      "obj", "pdf",     "doc",   "docx", "xls", "xlsx",
            "ppt",   "pptx", "ttf",    "otf", "woff",    "woff2", "eot",  "pyc", "pyo",
            "class", "jar",  "sqlite", "db",  "sqlite3", "wasm",  NULL};
        for (int i = 0; binary_exts[i]; i++) {
            if (strcasecmp(ext, binary_exts[i]) == 0)
                return true;
        }
    }

    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    unsigned char buf[NC_CMD_MAX];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);

    return text_is_binary_data(buf, n);
}

const char *text_encoding_name(const unsigned char *data, size_t len) {
    return encoding_name(text_detect_encoding(data, len));
}

size_t text_valid_utf8_prefix(const unsigned char *data, size_t len) {
    size_t valid = 0;
    for (size_t i = 0; i < len;) {
        unsigned char c = data[i];
        int char_len = 1;

        if (c < 0x80) {
            char_len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            char_len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            char_len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            char_len = 4;
        } else {
            break;
        }

        if (i + char_len > len)
            break;

        bool ok = true;
        for (int j = 1; j < char_len; j++) {
            if ((data[i + j] & 0xC0) != 0x80) {
                ok = false;
                break;
            }
        }

        if (!ok)
            break;
        i += char_len;
        valid = i;
    }
    return valid;
}

/*
 * Strip lone UTF-16 surrogates encoded in UTF-8 (ED A0 80 .. ED BF BF).
 * Rich-text clipboard pastes can inject these; they are valid UTF-8 byte
 * sequences but invalid Unicode codepoints and crash JSON serializers.
 * Returns a newly allocated copy with surrogates removed.
 */
char *text_strip_surrogates(const char *input, size_t len) {
    if (!input || len == 0)
        return nc_xstrdup("");

    char *out = malloc(len + 1);
    if (!out)
        return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len;) {
        unsigned char c = (unsigned char)input[i];
        if (c == 0xED && i + 2 < len) {
            unsigned char c1 = (unsigned char)input[i + 1];
            if (c1 >= 0xA0 && c1 <= 0xBF && ((unsigned char)input[i + 2] & 0xC0) == 0x80) {
                i += 3;
                continue;
            }
        }
        out[j++] = input[i++];
    }
    out[j] = '\0';
    return out;
}

bool text_has_surrogates(const char *data, size_t len) {
    if (!data)
        return false;
    for (size_t i = 0; i + 2 < len; i++) {
        if ((unsigned char)data[i] == 0xED) {
            unsigned char c1 = (unsigned char)data[i + 1];
            if (c1 >= 0xA0 && c1 <= 0xBF && ((unsigned char)data[i + 2] & 0xC0) == 0x80)
                return true;
        }
    }
    return false;
}

char *text_sanitize_for_display(const unsigned char *data, size_t len) {
    if (!data || len == 0)
        return nc_xstrdup("");

    size_t valid = text_valid_utf8_prefix(data, len);
    if (valid == len) {
        char *out = malloc(len + 1);
        if (!out)
            return NULL;
        memcpy(out, data, len);
        out[len] = '\0';
        return out;
    }

    char *out = malloc(valid + NC_TRUNC_SUFFIX_MAX);
    if (!out)
        return NULL;
    memcpy(out, data, valid);
    snprintf(out + valid, NC_TRUNC_SUFFIX_MAX, " [... binary data truncated]");
    return out;
}
