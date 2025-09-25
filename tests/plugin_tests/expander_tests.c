// tests/plugin_tests/expander_tests.c
// Unit tests for expander plugin's plugin_transform()
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

/* ---------- Stubs visible to expander.c ---------- */
/* We test only plugin_transform; provide tiny stubs so we don't link the SDK. */
int is_end(const char* s) { return (s != NULL) && (strcmp(s, "<END>") == 0); }
const char* common_plugin_init(const char* (*process_function)(const char*),
                               const char* name, int queue_size) {
    (void)process_function; (void)name; (void)queue_size;
    return NULL; /* no-op in unit tests */
}

/* Include the plugin under test after the stubs */
#include "../../plugins/expander.c"

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
    report_test("expander: NULL input returns NULL", out == NULL);
}

static void test_end_token_passthrough(void) {
    const char* in = "<END>";
    const char* out = plugin_transform(in);
    report_test("expander: <END> is passed through unchanged (same pointer)", out == in);
}

static void test_empty_string_passthrough(void) {
    const char* in = "";
    const char* out = plugin_transform(in);
    report_test("expander: empty string is a no-op (same pointer)", out == in);
}

static void test_single_char_passthrough(void) {
    const char* in = "A";
    const char* out = plugin_transform(in);
    report_test("expander: single char is a no-op (same pointer)", out == in);
}

static void test_basic_expansion(void) {
    const char* in = "ABC";
    const char* out = plugin_transform(in);
    int ok = (out != in) && (strcmp(out, "A B C") == 0);
    report_test("expander: \"ABC\" -> \"A B C\"", ok);
    free_if_needed(in, out);
}

static void test_with_punctuation(void) {
    const char* in = "A!B";
    const char* out = plugin_transform(in);
    int ok = (out != in) && (strcmp(out, "A ! B") == 0);
    report_test("expander: with punctuation \"A!B\" -> \"A ! B\"", ok);
    free_if_needed(in, out);
}

static void test_with_existing_space_middle(void) {
    const char* in = "A B";      /* A␠B */
    const char* out = plugin_transform(in);
    /* Spaces are added between every adjacent pair, so we expect 3 spaces total between A and B */
    int ok = (out != in) && (strcmp(out, "A   B") == 0); /* A␠␠␠B */
    report_test("expander: existing middle space \"A B\" -> \"A   B\"", ok);
    free_if_needed(in, out);
}

static void test_leading_trailing_spaces(void) {
    const char* in = " ab ";     /* ␠ a b ␠ */
    const char* out = plugin_transform(in);
    /* After inserting spaces between every pair, expect two spaces at start and end: "␠␠ a b ␠␠" */
    int ok = (out != in) && (strcmp(out, "  a b  ") == 0);
    report_test("expander: leading/trailing spaces \" ab \" -> \"  a b  \"", ok);
    free_if_needed(in, out);
}

static void test_digits_and_symbols_preserved(void) {
    const char* in = "1#2";
    const char* out = plugin_transform(in);
    int ok = (out != in) && (strcmp(out, "1 # 2") == 0);
    report_test("expander: digits & symbols \"1#2\" -> \"1 # 2\"", ok);
    free_if_needed(in, out);
}

static void test_long_string_near_limit(void) {
    size_t len = 1000;
    char* in = (char*)malloc(len + 1);
    char* expected = NULL;
    int ok = (in != NULL);

    if (!ok) {
        report_test("expander: long string allocation (input)", 0);
        free(in);
        return;
    }

    /* Build a predictable pattern: a Z 9 # repeating */
    for (size_t i = 0; i < len; ++i) {
        switch (i % 4) { case 0: in[i] = 'a'; break; case 1: in[i] = 'Z'; break; case 2: in[i] = '9'; break; default: in[i] = '#'; break; }
    }
    in[len] = '\0';

    /* expected length = len + (len - 1) + 1 for NUL */
    size_t out_len = len + (len - 1);
    expected = (char*)malloc(out_len + 1);
    ok = ok && (expected != NULL);
    if (!ok) {
        report_test("expander: long string allocation (expected)", 0);
        free(in);
        free(expected);
        return;
    }

    /* Build expected by inserting a single space between each pair */
    size_t j = 0;
    for (size_t i = 0; i < len; ++i) {
        expected[j++] = in[i];
        if (i < len - 1) expected[j++] = ' ';
    }
    expected[out_len] = '\0';

    const char* out = plugin_transform(in);
    ok = ok && (out != in) && (strcmp(out, expected) == 0);
    report_test("expander: long string near limit (1000 chars)", ok);

    free_if_needed(in, out);
    free(in);
    free(expected);
}

/* ---------- Main ---------- */
int main(void) {
    fprintf(stderr, "======== [EXPANDER UNIT TESTS] ========\n");
    fprintf(stderr, "\n");

    test_null_input();
    test_end_token_passthrough();
    test_empty_string_passthrough();
    test_single_char_passthrough();
    test_basic_expansion();
    test_with_punctuation();
    test_with_existing_space_middle();
    test_leading_trailing_spaces();
    test_digits_and_symbols_preserved();
    test_long_string_near_limit();

    fprintf(stderr, "\n");
    if (tests_failed == 0) {
        fprintf(stderr, "%sAll expander tests passed.%s\n", GREEN, NC);
        return EXIT_SUCCESS;
    } else {
        fprintf(stderr, "%sSome expander tests FAILED (%d).%s\n", RED, tests_failed, NC);
        return EXIT_FAILURE;
    }
}
