#include "nevercode.h"

/* IV_MAX_FILE_SIZE in defaults.h */

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const unsigned char *data, size_t len) {
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = malloc(out_len + 1);
    if (!out)
        return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t a = i < len ? data[i++] : 0;
        uint32_t b = i < len ? data[i++] : 0;
        uint32_t c = i < len ? data[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (i > len + 1) ? '=' : b64_table[(triple >> 6) & 0x3F];
        out[j++] = (i > len) ? '=' : b64_table[triple & 0x3F];
    }
    out[j] = '\0';
    return out;
}

static const char *ext_lower(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot ? dot + 1 : "";
}

const char *image_media_type_for_ext(const char *ext) {
    if (!ext)
        return NULL;
    char lower[NC_NUM_MAX] = {0};
    for (int i = 0; ext[i] && i < 15; i++)
        lower[i] = tolower((unsigned char)ext[i]);

    if (strcmp(lower, "png") == 0)
        return "image/png";
    if (strcmp(lower, "jpg") == 0)
        return "image/jpeg";
    if (strcmp(lower, "jpeg") == 0)
        return "image/jpeg";
    if (strcmp(lower, "gif") == 0)
        return "image/gif";
    if (strcmp(lower, "webp") == 0)
        return "image/webp";
    if (strcmp(lower, "svg") == 0)
        return "image/svg+xml";
    return NULL;
}

bool is_image_file(const char *path) {
    if (!path)
        return false;
    const char *ext = ext_lower(path);
    return image_media_type_for_ext(ext) != NULL;
}

char *image_read_base64(const char *path, char **media_type) {
    if (!path)
        return NULL;

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return NULL;
    if (st.st_size <= 0 || st.st_size > IV_MAX_FILE_SIZE)
        return NULL;

    const char *ext = ext_lower(path);
    const char *mt = image_media_type_for_ext(ext);
    if (!mt)
        return NULL;

    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    unsigned char *buf = malloc(st.st_size);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, st.st_size, f);
    fclose(f);

    if ((off_t)n != st.st_size) {
        free(buf);
        return NULL;
    }

    char *b64 = base64_encode(buf, n);
    free(buf);

    if (b64 && media_type)
        *media_type = nc_xstrdup(mt);

    return b64;
}
