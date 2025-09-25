// tests/plugin_tests/uppercaser_tests.c
// Unit tests for uppercaser plugin's plugin_transform()
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

/* ---------- Stubs visible to uppercaser.c ---------- */
/* We test only plugin_transform; provide tiny stubs so we don't link the SDK. */
int is_end(const char* s) { return (s != NULL) && (strcmp(s, "<END>") == 0); }
const char* common_plugin_init(const char* (*process_function)(const char*),
                               const char* name, int queue_size) {
    (void)process_function; (void)name; (void)queue_size;
    return NULL; /* no-op in unit tests */
}

/* Include the plugin under test after the stubs */
#include "../../plugins/uppercaser.c"

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
    report_test("uppercaser: NULL input returns NULL", out == NULL);
}

static void test_end_token_passthrough(void) {
    const char* in = "<END>";
    const char* out = plugin_transform(in);
    report_test("uppercaser: <END> is passed through unchanged (same pointer)", out == in);
}

static void test_empty_string_passthrough(void) {
    const char* in = "";
    const char* out = plugin_transform(in);
    int ok = (out == in);
    report_test("uppercaser: empty string is a no-op (same pointer)", ok);
}

static void test_mixed_case_conversion(void) {
    const char* in = "HeLlo 123!";
    const char* out = plugin_transform(in);
    int ok = (out != in) && (strcmp(out, "HELLO 123!") == 0);
    report_test("uppercaser: mixed case -> HELLO 123!", ok);
    free_if_needed(in, out);
}

static void test_already_uppercase_copy_same_content(void) {
    const char* in = "ALREADY UPPER!";
    const char* out = plugin_transform(in);
    int ok = (out != in) && (strcmp(out, "ALREADY UPPER!") == 0);
    report_test("uppercaser: already uppercase -> identical content (new buffer)", ok);
    free_if_needed(in, out);
}

static void test_no_letters_copy_same_content(void) {
    const char* in = "123! @#";
    const char* out = plugin_transform(in);
    int ok = (out != in) && (strcmp(out, "123! @#") == 0);
    report_test("uppercaser: no letters -> identical content (new buffer)", ok);
    free_if_needed(in, out);
}

static void test_single_char_lower_upper(void) {
    const char* in1 = "a";
    const char* out1 = plugin_transform(in1);
    int ok1 = (out1 != in1) && (strcmp(out1, "A") == 0);
    free_if_needed(in1, out1);

    const char* in2 = "A";
    const char* out2 = plugin_transform(in2);
    int ok2 = (out2 != in2) && (strcmp(out2, "A") == 0);
    free_if_needed(in2, out2);

    report_test("uppercaser: single-char lower/upper", ok1 && ok2);
}

static void test_long_string_near_limit(void) {
    size_t len = 1000;
    char* in = (char*)malloc(len + 1);
    char* expected = (char*)malloc(len + 1);
    int ok = (in != NULL) && (expected != NULL);

    if (!ok) {
        report_test("uppercaser: long string allocation", 0);
        free(in); free(expected);
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        char ch;
        switch (i % 3) { case 0: ch = 'a'; break; case 1: ch = 'Z'; break; default: ch = '9'; }
        in[i] = ch;
        expected[i] = (ch == 'a') ? 'A' : ch;
    }
    in[len] = '\0';
    expected[len] = '\0';

    const char* out = plugin_transform(in);
    ok = ok && (out != in) && (strcmp(out, expected) == 0);
    report_test("uppercaser: long string near limit (1000 chars)", ok);

    free_if_needed(in, out);
    free(in);
    free(expected);
}

/* ---------- Main ---------- */
int main(void) {
    fprintf(stderr, "======== [UPPERCASER UNIT TESTS] ========\n");
    fprintf(stderr, "\n");

    test_null_input();
    test_end_token_passthrough();
    test_empty_string_passthrough();
    test_mixed_case_conversion();
    test_already_uppercase_copy_same_content();
    test_no_letters_copy_same_content();
    test_single_char_lower_upper();
    test_long_string_near_limit();

    fprintf(stderr, "\n");

    if (tests_failed == 0) {
        fprintf(stderr, "%sAll uppercaser tests passed.%s\n", GREEN, NC);
        return EXIT_SUCCESS;
    } else {
        fprintf(stderr, "%sSome uppercaser tests FAILED (%d).%s\n", RED, tests_failed, NC);
        return EXIT_FAILURE;
    }
}
