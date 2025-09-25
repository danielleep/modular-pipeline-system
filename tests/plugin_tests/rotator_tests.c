// tests/plugin_tests/rotator_tests.c
// Unit tests for rotator plugin's plugin_transform()
// - Verifies return values and string content
// - No threads/queues/dlopen involved

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Colors ---------- */
#define GREEN  "\033[0;32m"
#define RED    "\033[0;31m"
#define YELLOW "\033[1;33m"
#define NC     "\033[0m"

/* ---------- Stubs visible to rotator.c ---------- */
/* We test only plugin_transform; provide tiny stubs so we don't link the SDK. */
int is_end(const char* s) { return (s != NULL) && (strcmp(s, "<END>") == 0); }
const char* common_plugin_init(const char* (*process_function)(const char*),
                               const char* name, int queue_size) {
    (void)process_function; (void)name; (void)queue_size;
    return NULL; /* no-op in unit tests */
}

/* Include the plugin under test after the stubs */
#include "../../plugins/rotator.c"

/* ---------- Tiny assertions & reporting ---------- */
static int tests_failed = 0;

static void report_test(const char* name, int ok) {
    if (ok) fprintf(stderr, "%s[PASS]%s %s\n", GREEN, NC, name);
    else    { fprintf(stderr, "%s[FAIL]%s %s\n", RED, NC, name); tests_failed++; }
}

static void free_if_needed(const char* in, const char* out) {
    if (out && out != in) free((void*)out);
}

/* ---------- Individual tests ---------- */

static void test_null_input(void) {
    const char* in = NULL;
    const char* out = plugin_transform(in);
    report_test("rotator: NULL input returns NULL", out == NULL);
}

static void test_end_token_passthrough(void) {
    const char* in = "<END>";
    const char* out = plugin_transform(in);
    report_test("rotator: <END> is passed through unchanged (same pointer)", out == in);
}

static void test_empty_string_passthrough(void) {
    const char* in = "";
    const char* out = plugin_transform(in);
    report_test("rotator: empty string is a no-op (same pointer)", out == in);
}

static void test_single_char_passthrough(void) {
    const char* in = "X";
    const char* out = plugin_transform(in);
    report_test("rotator: single char is a no-op (same pointer)", out == in);
}

static void test_len2_swap(void) {
    const char* in = "ab";
    const char* out = plugin_transform(in);
    int ok = (out != in) && (strcmp(out, "ba") == 0);
    report_test("rotator: length-2 \"ab\" -> \"ba\"", ok);
    free_if_needed(in, out);
}

static void test_even_length_rotate(void) {
    const char* in = "abcd";
    const char* out = plugin_transform(in);
    int ok = (out != in) && (strcmp(out, "dabc") == 0);
    report_test("rotator: even length \"abcd\" -> \"dabc\"", ok);
    free_if_needed(in, out);
}

static void test_odd_length_rotate(void) {
    const char* in = "abcde";
    const char* out = plugin_transform(in);
    int ok = (out != in) && (strcmp(out, "eabcd") == 0);
    report_test("rotator: odd length \"abcde\" -> \"eabcd\"", ok);
    free_if_needed(in, out);
}

static void test_spaces_and_punctuation_preserved(void) {
    const char* in = "A b! 1";
    const char* out = plugin_transform(in);
    int ok = (out != in) && (strcmp(out, "1A b! ") == 0);
    report_test("rotator: preserves spaces & punctuation (\"A b! 1\" -> \"1A b! \")", ok);
    free_if_needed(in, out);
}

static void test_leading_trailing_spaces(void) {
    const char* in = " ab ";
    const char* out = plugin_transform(in);
    int ok = (out != in) && (strcmp(out, "  ab") == 0);
    report_test("rotator: leading/trailing spaces (\" ab \" -> \"  ab\")", ok);
    free_if_needed(in, out);
}

static void test_long_string_near_limit(void) {
    size_t len = 1000;
    char* in = (char*)malloc(len + 1);
    char* expected = (char*)malloc(len + 1);
    int ok = (in != NULL) && (expected != NULL);

    if (!ok) {
        report_test("rotator: long string allocation", 0);
        free(in); free(expected);
        return;
    }

    /* Build a predictable pattern */
    for (size_t i = 0; i < len; ++i) {
        char ch;
        switch (i % 4) {
            case 0: ch = 'a'; break;
            case 1: ch = 'Z'; break;
            case 2: ch = '9'; break;
            default: ch = '#'; break;
        }
        in[i] = ch;
    }
    in[len] = '\0';

    /* expected = right-rotate by one */
    expected[0] = in[len - 1];
    for (size_t i = 0; i < len - 1; ++i) expected[i + 1] = in[i];
    expected[len] = '\0';

    const char* out = plugin_transform(in);
    ok = ok && (out != in) && (strcmp(out, expected) == 0);
    report_test("rotator: long string near limit (1000 chars)", ok);

    free_if_needed(in, out);
    free(in);
    free(expected);
}

/* ---------- Main ---------- */
int main(void) {
    fprintf(stderr, "======== [ROTATOR UNIT TESTS] ========\n");
    fprintf(stderr, "\n");

    test_null_input();
    test_end_token_passthrough();
    test_empty_string_passthrough();
    test_single_char_passthrough();
    test_len2_swap();
    test_even_length_rotate();
    test_odd_length_rotate();
    test_spaces_and_punctuation_preserved();
    test_leading_trailing_spaces();
    test_long_string_near_limit();
    fprintf(stderr, "\n");

    if (tests_failed == 0) {
        fprintf(stderr, "%sAll rotator tests passed.%s\n", GREEN, NC);
        return EXIT_SUCCESS;
    } else {
        fprintf(stderr, "%sSome rotator tests FAILED (%d).%s\n", RED, tests_failed, NC);
        return EXIT_FAILURE;
    }
}
