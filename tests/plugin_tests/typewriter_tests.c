// tests/plugin_tests/typewriter_tests.c
// Unit tests for typewriter plugin's plugin_transform()
// - Verifies return values and exact STDOUT output (with delay disabled)
// - No threads/queues/dlopen involved

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* ---------- Colors ---------- */
#define GREEN  "\033[0;32m"
#define RED    "\033[0;31m"
#define YELLOW "\033[1;33m"
#define NC     "\033[0m"

/* ---------- Disable delay inside the plugin during tests ---------- */
#define usleep(x) (void)0

/* ---------- Stubs visible to typewriter.c ---------- */
/* We test only plugin_transform; provide tiny stubs so we don't link the SDK. */
int is_end(const char* s) { return (s != NULL) && (strcmp(s, "<END>") == 0); }
const char* common_plugin_init(const char* (*process_function)(const char*),
                               const char* name, int queue_size) {
    (void)process_function; (void)name; (void)queue_size;
    return NULL; /* no-op in unit tests */
}

/* Include the plugin under test after the stubs & usleep macro */
#include "../../plugins/typewriter.c"

/* ---------- Minimal stdout capture helper (POSIX) ---------- */
typedef struct {
    int saved_fd;    /* original stdout fd backup */
    int read_fd;     /* pipe read end */
} capture_t;

static int capture_begin(capture_t* cap) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    fflush(stdout);
    cap->saved_fd = dup(STDOUT_FILENO);
    if (cap->saved_fd < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }

    /* Redirect stdout to pipe write end */
    if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
        close(pipefd[0]); close(pipefd[1]); close(cap->saved_fd);
        return -1;
    }
    close(pipefd[1]); /* writer is now STDOUT_FILENO */
    cap->read_fd = pipefd[0];
    return 0;
}

/* Restores stdout and returns a newly-allocated buffer with all captured bytes */
static char* capture_end(capture_t* cap) {
    /* Restore stdout */
    fflush(stdout);
    if (dup2(cap->saved_fd, STDOUT_FILENO) >= 0) {
        close(cap->saved_fd);
    }

    /* Read everything from the pipe into a dynamic buffer */
    size_t capsz = 1024, len = 0;
    char* buf = (char*)malloc(capsz);
    if (!buf) { close(cap->read_fd); return NULL; }

    for (;;) {
        if (len + 512 > capsz) {
            capsz *= 2;
            char* nb = (char*)realloc(buf, capsz);
            if (!nb) { free(buf); close(cap->read_fd); return NULL; }
            buf = nb;
        }
        ssize_t n = read(cap->read_fd, buf + len, (capsz - len) - 1);
        if (n > 0) {
            len += (size_t)n;
        } else if (n == 0) {
            break; /* EOF */
        } else {
            if (errno == EINTR) continue;
            break; /* other errors: stop and use what we have */
        }
    }
    close(cap->read_fd);
    buf[len] = '\0';
    return buf;
}

/* ---------- Tiny assertions & reporting ---------- */
static int tests_failed = 0;

static void report_test(const char* name, int ok) {
    if (ok) {
        fprintf(stderr, "%s[PASS]%s %s\n", GREEN, NC, name);
    } else {
        fprintf(stderr, "%s[FAIL]%s %s\n", RED,   NC, name);
        tests_failed++;
    }
}

/* Convenience to build expected "[typewriter] " + input + "\n" */
static char* build_expected(const char* input) {
    const char* prefix = "[typewriter] ";
    size_t lp = strlen(prefix), li = strlen(input);
    char* s = (char*)malloc(lp + li + 2);
    if (!s) return NULL;
    memcpy(s, prefix, lp);
    memcpy(s + lp, input, li);
    s[lp + li] = '\n';
    s[lp + li + 1] = '\0';
    return s;
}

/* ---------- Individual tests ---------- */

static void test_null_input(void) {
    const char* in = NULL;

    capture_t cap; capture_begin(&cap);
    const char* out = plugin_transform(in);
    fflush(stdout);
    char* captured = capture_end(&cap);

    int ok = (out == NULL) && captured && (strcmp(captured, "") == 0);
    report_test("typewriter: NULL input returns NULL and prints nothing", ok);

    free(captured);
}

static void test_end_token_no_output(void) {
    const char* in = "<END>";

    capture_t cap; capture_begin(&cap);
    const char* out = plugin_transform(in);
    fflush(stdout);
    char* captured = capture_end(&cap);

    int ok = (out == in) && captured && (strcmp(captured, "") == 0);
    report_test("typewriter: <END> prints nothing and passes pointer through", ok);

    free(captured);
}

static void test_empty_string_prints_header_only(void) {
    const char* in = "";

    capture_t cap; capture_begin(&cap);
    const char* out = plugin_transform(in);
    fflush(stdout);
    char* captured = capture_end(&cap);

    char* expected = build_expected(in);
    int ok = (out == in) && captured && expected && (strcmp(captured, expected) == 0);
    report_test("typewriter: empty string prints \"[typewriter] \\n\"", ok);

    free(captured);
    free(expected);
}

static void test_short_text_hi(void) {
    const char* in = "Hi";

    capture_t cap; capture_begin(&cap);
    const char* out = plugin_transform(in);
    fflush(stdout);
    char* captured = capture_end(&cap);

    char* expected = build_expected(in);
    int ok = (out == in) && captured && expected && (strcmp(captured, expected) == 0);
    report_test("typewriter: \"Hi\" -> \"[typewriter] Hi\\n\"", ok);

    free(captured);
    free(expected);
}

static void test_punctuation_and_spaces(void) {
    const char* in = "A b! 1";

    capture_t cap; capture_begin(&cap);
    const char* out = plugin_transform(in);
    fflush(stdout);
    char* captured = capture_end(&cap);

    char* expected = build_expected(in);
    int ok = (out == in) && captured && expected && (strcmp(captured, expected) == 0);
    report_test("typewriter: preserves spaces & punctuation", ok);

    free(captured);
    free(expected);
}

static void test_digits_only(void) {
    const char* in = "12345";

    capture_t cap; capture_begin(&cap);
    const char* out = plugin_transform(in);
    fflush(stdout);
    char* captured = capture_end(&cap);

    char* expected = build_expected(in);
    int ok = (out == in) && captured && expected && (strcmp(captured, expected) == 0);
    report_test("typewriter: digits only", ok);

    free(captured);
    free(expected);
}

static void test_leading_trailing_spaces(void) {
    const char* in = " ab ";

    capture_t cap; capture_begin(&cap);
    const char* out = plugin_transform(in);
    fflush(stdout);
    char* captured = capture_end(&cap);

    char* expected = build_expected(in);
    int ok = (out == in) && captured && expected && (strcmp(captured, expected) == 0);
    report_test("typewriter: leading/trailing spaces", ok);

    free(captured);
    free(expected);
}

static void test_medium_length_text(void) {
    /* Not too long to avoid noisy output in test logs */
    size_t len = 250;
    char* in = (char*)malloc(len + 1);
    if (!in) {
        report_test("typewriter: medium string allocation", 0);
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        switch (i % 4) { case 0: in[i] = 'a'; break; case 1: in[i] = 'Z'; break; case 2: in[i] = '9'; break; default: in[i] = '#'; break; }
    }
    in[len] = '\0';

    capture_t cap; capture_begin(&cap);
    const char* out = plugin_transform(in);
    fflush(stdout);
    char* captured = capture_end(&cap);

    char* expected = build_expected(in);
    int ok = (out == in) && captured && expected && (strcmp(captured, expected) == 0);
    report_test("typewriter: medium length text (~250 chars)", ok);

    free(captured);
    free(expected);
    free(in);
}

/* ---------- Main ---------- */
int main(void) {
    fprintf(stderr, "======== [TYPEWRITER UNIT TESTS] ========\n");
    fprintf(stderr, "\n");

    test_null_input();
    test_end_token_no_output();
    test_empty_string_prints_header_only();
    test_short_text_hi();
    test_punctuation_and_spaces();
    test_digits_only();
    test_leading_trailing_spaces();
    test_medium_length_text();

    fprintf(stderr, "\n");
    if (tests_failed == 0) {
        fprintf(stderr, "%sAll typewriter tests passed.%s\n", GREEN, NC);
        return EXIT_SUCCESS;
    } else {
        fprintf(stderr, "%sSome typewriter tests FAILED (%d).%s\n", RED, tests_failed, NC);
        return EXIT_FAILURE;
    }
}
