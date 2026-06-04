#include "test.h"
#include "nevercode.h"

static int test_png(void) {
    ASSERT_TRUE(is_image_file("screenshot.png"));
    return 0;
}

static int test_jpg(void) {
    ASSERT_TRUE(is_image_file("photo.jpg"));
    return 0;
}

static int test_jpeg(void) {
    ASSERT_TRUE(is_image_file("img.jpeg"));
    return 0;
}

static int test_gif(void) {
    ASSERT_TRUE(is_image_file("anim.gif"));
    return 0;
}

static int test_webp(void) {
    ASSERT_TRUE(is_image_file("pic.webp"));
    return 0;
}

static int test_txt_not_image(void) {
    ASSERT_FALSE(is_image_file("notes.txt"));
    return 0;
}

static int test_c_not_image(void) {
    ASSERT_FALSE(is_image_file("main.c"));
    return 0;
}

static int test_no_ext(void) {
    ASSERT_FALSE(is_image_file("Makefile"));
    return 0;
}

static int test_media_type(void) {
    const char *mt = image_media_type_for_ext("png");
    ASSERT_NOT_NULL(mt);
    ASSERT_STR_CONTAINS(mt, "png");
    return 0;
}

int test_image_all(void) {
    TEST_COUNTERS;
    TEST_SUITE("image");
    RUN_TEST(test_png);
    RUN_TEST(test_jpg);
    RUN_TEST(test_jpeg);
    RUN_TEST(test_gif);
    RUN_TEST(test_webp);
    RUN_TEST(test_txt_not_image);
    RUN_TEST(test_c_not_image);
    RUN_TEST(test_no_ext);
    RUN_TEST(test_media_type);
    TEST_RETURN_FAILED();
}
