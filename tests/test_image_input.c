#include "test.h"
#include "nevercode.h"
#include "conversation.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int test_image_png(void) {
    ASSERT(is_image_file("photo.png"));
    ASSERT(is_image_file("dir/image.PNG"));
    return 0;
}

static int test_image_jpg(void) {
    ASSERT(is_image_file("photo.jpg"));
    ASSERT(is_image_file("photo.jpeg"));
    ASSERT(is_image_file("dir/PHOTO.JPG"));
    return 0;
}

static int test_image_webp_gif(void) {
    ASSERT(is_image_file("anim.gif"));
    ASSERT(is_image_file("modern.webp"));
    return 0;
}

static int test_image_svg(void) {
    ASSERT(is_image_file("icon.svg"));
    return 0;
}

static int test_not_image(void) {
    ASSERT(!is_image_file("code.c"));
    ASSERT(!is_image_file("readme.md"));
    ASSERT(!is_image_file("data.json"));
    ASSERT(!is_image_file(NULL));
    ASSERT(!is_image_file(""));
    return 0;
}

static int test_media_type_png(void) {
    const char *mt = image_media_type_for_ext("png");
    ASSERT(mt && strcmp(mt, "image/png") == 0);
    return 0;
}

static int test_media_type_jpeg(void) {
    const char *mt = image_media_type_for_ext("jpg");
    ASSERT(mt && strcmp(mt, "image/jpeg") == 0);
    mt = image_media_type_for_ext("jpeg");
    ASSERT(mt && strcmp(mt, "image/jpeg") == 0);
    return 0;
}

static int test_media_type_unknown(void) {
    ASSERT(image_media_type_for_ext("txt") == NULL);
    ASSERT(image_media_type_for_ext(NULL) == NULL);
    return 0;
}

static int test_image_read_base64_nonexistent(void) {
    char *mt = NULL;
    char *b64 = image_read_base64("/nonexistent/file.png", &mt);
    ASSERT(b64 == NULL);
    ASSERT(mt == NULL);
    return 0;
}

static int test_image_read_base64_real(void) {
    static const unsigned char tiny_png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
        0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00,
        0x00, 0x90, 0x77, 0x53, 0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, 0x08,
        0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0xe2, 0x21, 0xbc,
        0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    const char *path = "/tmp/nevercode_test_tiny.png";
    FILE *f = fopen(path, "wb");
    ASSERT(f != NULL);
    fwrite(tiny_png, 1, sizeof(tiny_png), f);
    fclose(f);

    char *mt = NULL;
    char *b64 = image_read_base64(path, &mt);
    ASSERT(b64 != NULL);
    ASSERT(mt != NULL);
    ASSERT_STR_EQ(mt, "image/png");
    ASSERT(strlen(b64) > 10);

    free(b64);
    free(mt);
    unlink(path);
    return 0;
}

static int test_message_add_image(void) {
    message_t m;
    message_init(&m, ROLE_USER);
    message_add_text(&m, "What is in this image?");
    message_add_image(&m, "iVBOR...", "image/png");
    ASSERT_INT_EQ(m.num_blocks, 2);
    ASSERT(m.blocks[0].type == CONTENT_TEXT);
    ASSERT(m.blocks[1].type == CONTENT_IMAGE);
    ASSERT_STR_EQ(m.blocks[1].image_data, "iVBOR...");
    ASSERT_STR_EQ(m.blocks[1].image_media_type, "image/png");
    message_free(&m);
    return 0;
}

static int test_inject_images_no_at(void) {
    message_t m;
    message_init(&m, ROLE_USER);
    int n = context_inject_images("hello world", &m);
    ASSERT_INT_EQ(n, 0);
    /* No @ found => returns 0, doesn't add blocks (caller handles fallback) */
    ASSERT_INT_EQ(m.num_blocks, 0);
    message_free(&m);
    return 0;
}

static int test_inject_images_with_file(void) {
    static const unsigned char tiny_png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
        0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00,
        0x00, 0x90, 0x77, 0x53, 0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, 0x08,
        0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0xe2, 0x21, 0xbc,
        0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    const char *path = "/tmp/nevercode_inject_test.png";
    FILE *f = fopen(path, "wb");
    fwrite(tiny_png, 1, sizeof(tiny_png), f);
    fclose(f);

    message_t m;
    message_init(&m, ROLE_USER);
    int n = context_inject_images("describe @/tmp/nevercode_inject_test.png please", &m);
    ASSERT_INT_EQ(n, 1);

    int found_text = 0, found_img = 0;
    for (int i = 0; i < m.num_blocks; i++) {
        if (m.blocks[i].type == CONTENT_TEXT)
            found_text = 1;
        if (m.blocks[i].type == CONTENT_IMAGE)
            found_img = 1;
    }
    ASSERT(found_text);
    ASSERT(found_img);

    message_free(&m);
    unlink(path);
    return 0;
}

int test_image_input_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("image_input");
    RUN_TEST(test_image_png);
    RUN_TEST(test_image_jpg);
    RUN_TEST(test_image_webp_gif);
    RUN_TEST(test_image_svg);
    RUN_TEST(test_not_image);
    RUN_TEST(test_media_type_png);
    RUN_TEST(test_media_type_jpeg);
    RUN_TEST(test_media_type_unknown);
    RUN_TEST(test_image_read_base64_nonexistent);
    RUN_TEST(test_image_read_base64_real);
    RUN_TEST(test_message_add_image);
    RUN_TEST(test_inject_images_no_at);
    RUN_TEST(test_inject_images_with_file);
    TEST_RETURN_FAILED();
}
