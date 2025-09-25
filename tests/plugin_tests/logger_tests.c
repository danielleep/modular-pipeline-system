// tests/plugin_tests/logger_tests.c
// Unit tests for logger plugin's plugin_transform()
// - Verifies return values and exact STDOUT output
// - No threads/queues/dlopen involved

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* Stub for is_end("<END>") used by logger.c */
int is_end(const char* s) { return (s != NULL) && (strcmp(s, "<END>") == 0); }

/* Stub for common_plugin_init used indirectly by logger.c via plugin_init.
   We don't test init here, so a no-op stub keeps the linker happy. */
const char* common_plugin_init(const char* (*process_function)(const char*),
                               const char* name, int queue_size) {
    (void)process_function; (void)name; (void)queue_size;
    return NULL;
}

#include "../../plugins/logger.c"

/* ---------- Colors ---------- */
#define GREEN  "\033[0;32m"
#define RED    "\033[0;31m"
#define YELLOW "\033[1;33m"
#define NC     "\033[0m"





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
            /* On other errors, stop and use what we have */
            break;
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

/* ---------- Individual tests ---------- */

static void test_null_input(void) {
    const char* in = NULL;

    capture_t cap;
    capture_begin(&cap);
    const char* out = plugin_transform(in);
    fflush(stdout);
    char* captured = capture_end(&cap);

    int ok = (out == NULL) && captured && (strcmp(captured, "") == 0);
    report_test("logger: NULL input returns NULL and prints nothing", ok);

    free(captured);
}

static void test_end_token_no_output(void) {
    const char* in = "<END>";

    capture_t cap;
    capture_begin(&cap);
    const char* out = plugin_transform(in);
    fflush(stdout);
    char* captured = capture_end(&cap);

    int ok = (out == in) && captured && (strcmp(captured, "") == 0);
    report_test("logger: <END> prints nothing and passes pointer through", ok);

    free(captured);
}

static void test_empty_string_prints_header_only(void) {
    const char* in = "";

    capture_t cap;
    capture_begin(&cap);
    const char* out = plugin_transform(in);
    fflush(stdout);
    char* captured = capture_end(&cap);

    int ok = (out == in) && captured && (strcmp(captured, "[logger] \n") == 0);
    report_test("logger: empty string prints \"[logger] \\n\"", ok);

    free(captured);
}

static void test_regular_string_prints_exactly(void) {
    const char* in = "hello";

    capture_t cap;
    capture_begin(&cap);
    const char* out = plugin_transform(in);
    fflush(stdout);
    char* captured = capture_end(&cap);

    int ok = (out == in) && captured && (strcmp(captured, "[logger] hello\n") == 0);
    report_test("logger: \"hello\" prints \"[logger] hello\\n\"", ok);

    free(captured);
}

static void test_punctuation_and_spaces_kept(void) {
    const char* in = "A b! 1";

    capture_t cap;
    capture_begin(&cap);
    const char* out = plugin_transform(in);
    fflush(stdout);
    char* captured = capture_end(&cap);

    int ok = (out == in) && captured && (strcmp(captured, "[logger] A b! 1\n") == 0);
    report_test("logger: keeps punctuation and spaces", ok);

    free(captured);
}

/* ---------- Main ---------- */
int main(void) {
    fprintf(stderr, "======== [LOGGER UNIT TESTS] ========\n");
    fprintf(stderr, "\n");

    test_null_input();
    test_end_token_no_output();
    test_empty_string_prints_header_only();
    test_regular_string_prints_exactly();
    test_punctuation_and_spaces_kept();

    fprintf(stderr, "\n");

    if (tests_failed == 0) {
        fprintf(stderr, "%sAll logger tests passed.%s\n", GREEN, NC);
        return EXIT_SUCCESS;
    } else {
        fprintf(stderr, "%sSome logger tests FAILED (%d).%s\n", RED, tests_failed, NC);
        return EXIT_FAILURE;
    }
}
