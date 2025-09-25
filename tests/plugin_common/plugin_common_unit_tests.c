// File: plugin_common_unit_tests.c
// Location to run from: /operating systems/tests/plugin_common
// This file now includes unit tests 1-6 for plugin_common.h
// 1) log_error, 2) log_info, 3) plugin_get_name,
// 4) common_plugin_init, 5) plugin_init, 6) plugin_attach
// Notes:
//  - Colored PASS/FAIL output
//  - At the end print overall success if all tests passed
//  - Includes are relative to this test file's run location
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>

#include "../../plugins/plugin_common.h"

extern const char* plugin_init(int queue_size);


// ====== Colors & printing helpers ======
#define GREEN  "\033[0;32m"
#define RED    "\033[0;31m"
#define YELLOW "\033[1;33m"
#define NC     "\033[0m"

#define PRINT_STATUS(fmt, ...)  do { fprintf(stdout, GREEN "[PASS]" NC " " fmt "\n", ##__VA_ARGS__); } while(0)
#define PRINT_FAIL(fmt, ...)    do { fprintf(stdout, RED   "[FAIL]" NC " " fmt "\n", ##__VA_ARGS__); } while(0)

static int g_tests_run = 0;
static int g_tests_failed = 0;

static void mark_pass(const char* name) { ++g_tests_run; PRINT_STATUS("%s", name); }
static void mark_fail(const char* name, const char* why) { ++g_tests_run; ++g_tests_failed; PRINT_FAIL("%s: %s", name, why); }

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

// ====== Stream capture helpers (stderr) ======
typedef struct {
    int saved_fd;
    char path[256];
} capture_t;

static int start_capture_stream(FILE* stream, capture_t* cap, const char* path_hint) {
    if (!cap) return -1;
    snprintf(cap->path, sizeof(cap->path), "%s", path_hint ? path_hint : "stderr_capture.txt");
    fflush(stream);
    int fd = fileno(stream);
    cap->saved_fd = dup(fd);
    if (cap->saved_fd < 0) return -1;
    int tmpfd = open(cap->path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (tmpfd < 0) { close(cap->saved_fd); return -1; }
    if (dup2(tmpfd, fd) < 0) { close(tmpfd); close(cap->saved_fd); return -1; }
    close(tmpfd);
    return 0;
}

static int stop_capture_stream(FILE* stream, capture_t* cap, char** out_buf) {
    if (!cap) return -1;
    fflush(stream);
    int fd = fileno(stream);
    if (dup2(cap->saved_fd, fd) < 0) { close(cap->saved_fd); return -1; }
    close(cap->saved_fd);

    // Read captured file into memory
    FILE* f = fopen(cap->path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((sz > 0 ? (size_t)sz : 0) + 1);
    if (!buf) { fclose(f); return -1; }
    size_t n = (sz > 0) ? fread(buf, 1, (size_t)sz, f) : 0;
    buf[n] = '\0';
    fclose(f);

    if (out_buf) *out_buf = buf; else free(buf);
    // Cleanup file
    unlink(cap->path);
    return 0;
}

// ====== Minimal dummy process function for later tests (safe no-op) ======
static const char* dummy_process_return_same(const char* in) {
    // Return the same pointer; plugin_common's consumer will free it exactly once.
    return in;
}

// ====== Spy "next_place_work" functions for attach tests ======
static int g_spy1_calls = 0;
static int g_spy2_calls = 0;

static const char* next_place_work_spy1(const char* s) {
    (void)s;
    g_spy1_calls++;
    return NULL; // success
}

static const char* next_place_work_spy2(const char* s) {
    (void)s;
    g_spy2_calls++;
    return NULL; // success
}

// ---- Additional dummy process functions (for counts/new buffers) ----
static int g_process_calls = 0;

static const char* dummy_process_counting_same(const char* in) {
    g_process_calls++;
    return in; // return same pointer
}

static const char* __attribute__((unused))
dummy_process_counting_new(const char* in) {
    g_process_calls++;
    char* dup = strdup(in ? in : "");
    return dup; // return a new buffer that common will free later
}

// ---- Spy that captures the last forwarded string (for <END> checks) ----
static int   g_spy_end_calls = 0;
static char* g_spy_last_copied = NULL;

static const char* next_place_work_spy_capture(const char* s) {
    g_spy_end_calls++;
    free(g_spy_last_copied);
    g_spy_last_copied = strdup(s ? s : "");
    return NULL; // success
}

// ---- Wait thread helper for plugin_wait_finished blocking test ----
typedef struct { int done; const char* err; } wait_ctx_t;

static void* wait_thread(void* p) {
    wait_ctx_t* w = (wait_ctx_t*)p;
    w->err  = plugin_wait_finished();
    w->done = (w->err == NULL) ? 1 : -1;
    return NULL;
}


// ====== Convenience: finish & cleanup if initialized ======
static void finish_if_initialized(void) {
    // Try to send <END> and gracefully shut down; ignore errors if not initialized
    (void)plugin_place_work("<END>");
    (void)plugin_wait_finished();
    (void)plugin_fini();
}

// ---------------------------------------------------------------------
// ====== Test 1: log_error(plugin_context_t*, const char*) ======
static void test_log_error_basic_format(void) {
    const char* TEST = "log_error: basic format with name";
    capture_t cap;
    if (start_capture_stream(stderr, &cap, "stderr_log_error_1.txt") != 0) {
        mark_fail(TEST, "failed to start stderr capture");
        return;
    }

    plugin_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.name = "logger";
    log_error(&ctx, "boom");

    char* out = NULL;
    if (stop_capture_stream(stderr, &cap, &out) != 0) {
        mark_fail(TEST, "failed to stop stderr capture");
        return;
    }

    const char* expected = "[ERROR][logger] - boom\n";
    if (strcmp(out, expected) == 0) {
        mark_pass(TEST);
    } else {
        char why[256];
        snprintf(why, sizeof(why), "expected '%s' got '%s'", expected, out);
        mark_fail(TEST, why);
    }
    free(out);
}

static void test_log_error_null_context(void) {
    const char* TEST = "log_error: NULL context fallback";
    capture_t cap;
    if (start_capture_stream(stderr, &cap, "stderr_log_error_2.txt") != 0) {
        mark_fail(TEST, "failed to start stderr capture");
        return;
    }

    log_error(NULL, "x");

    char* out = NULL;
    if (stop_capture_stream(stderr, &cap, &out) != 0) {
        mark_fail(TEST, "failed to stop stderr capture");
        return;
    }

    const char* expected = "[ERROR][unknown] - x\n";
    if (strcmp(out, expected) == 0) {
        mark_pass(TEST);
    } else {
        char why[256];
        snprintf(why, sizeof(why), "expected '%s' got '%s'", expected, out);
        mark_fail(TEST, why);
    }
    free(out);
}

static void test_log_error_null_message(void) {
    const char* TEST = "log_error: NULL message fallback";
    capture_t cap;
    if (start_capture_stream(stderr, &cap, "stderr_log_error_3.txt") != 0) {
        mark_fail(TEST, "failed to start stderr capture");
        return;
    }

    plugin_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.name = "p";
    log_error(&ctx, NULL);

    char* out = NULL;
    if (stop_capture_stream(stderr, &cap, &out) != 0) {
        mark_fail(TEST, "failed to stop stderr capture");
        return;
    }

    const char* expected = "[ERROR][p] - unknown error\n";
    if (strcmp(out, expected) == 0) {
        mark_pass(TEST);
    } else {
        char why[256];
        snprintf(why, sizeof(why), "expected '%s' got '%s'", expected, out);
        mark_fail(TEST, why);
    }
    free(out);
}

// ====== Test 2: log_info(plugin_context_t*, const char*) ======
static void test_log_info_basic_format(void) {
    const char* TEST = "log_info: basic format with name";
    capture_t cap;
    if (start_capture_stream(stderr, &cap, "stderr_log_info_1.txt") != 0) {
        mark_fail(TEST, "failed to start stderr capture");
        return;
    }

    plugin_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.name = "uppercaser";
    log_info(&ctx, "ready");

    char* out = NULL;
    if (stop_capture_stream(stderr, &cap, &out) != 0) {
        mark_fail(TEST, "failed to stop stderr capture");
        return;
    }

    const char* expected = "[INFO][uppercaser] - ready\n";
    if (strcmp(out, expected) == 0) {
        mark_pass(TEST);
    } else {
        char why[256];
        snprintf(why, sizeof(why), "expected '%s' got '%s'", expected, out);
        mark_fail(TEST, why);
    }
    free(out);
}

static void test_log_info_null_context(void) {
    const char* TEST = "log_info: NULL context fallback";
    capture_t cap;
    if (start_capture_stream(stderr, &cap, "stderr_log_info_2.txt") != 0) {
        mark_fail(TEST, "failed to start stderr capture");
        return;
    }

    log_info(NULL, "x");

    char* out = NULL;
    if (stop_capture_stream(stderr, &cap, &out) != 0) {
        mark_fail(TEST, "failed to stop stderr capture");
        return;
    }

    const char* expected = "[INFO][unknown] - x\n";
    if (strcmp(out, expected) == 0) {
        mark_pass(TEST);
    } else {
        char why[256];
        snprintf(why, sizeof(why), "expected '%s' got '%s'", expected, out);
        mark_fail(TEST, why);
    }
    free(out);
}

static void test_log_info_null_message(void) {
    const char* TEST = "log_info: NULL message fallback";
    capture_t cap;
    if (start_capture_stream(stderr, &cap, "stderr_log_info_3.txt") != 0) {
        mark_fail(TEST, "failed to start stderr capture");
        return;
    }

    plugin_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.name = "p";
    log_info(&ctx, NULL);

    char* out = NULL;
    if (stop_capture_stream(stderr, &cap, &out) != 0) {
        mark_fail(TEST, "failed to stop stderr capture");
        return;
    }

    const char* expected = "[INFO][p] - no info\n";
    if (strcmp(out, expected) == 0) {
        mark_pass(TEST);
    } else {
        char why[256];
        snprintf(why, sizeof(why), "expected '%s' got '%s'", expected, out);
        mark_fail(TEST, why);
    }
    free(out);
}

// ====== Test 3: plugin_get_name(void) ======
static void test_plugin_get_name_before_init(void) {
    const char* TEST = "plugin_get_name: before init returns 'unknown'";
    const char* name = plugin_get_name();
    if (name && strcmp(name, "unknown") == 0) {
        mark_pass(TEST);
    } else {
        char why[256];
        snprintf(why, sizeof(why), "expected 'unknown' got '%s'", name ? name : "(null)");
        mark_fail(TEST, why);
    }
}

static void test_plugin_get_name_after_init_and_after_fini(void) {
    const char* TEST_A = "plugin_get_name: after init returns plugin name";
    const char* TEST_B = "plugin_get_name: after fini returns 'unknown'";

    const char* err = common_plugin_init(dummy_process_return_same, "dummy", 2);
    if (err != NULL) {
        mark_fail(TEST_A, "common_plugin_init failed (cannot proceed)");
        return;
    }

    const char* nm = plugin_get_name();
    if (nm && strcmp(nm, "dummy") == 0) {
        mark_pass(TEST_A);
    } else {
        char why[256];
        snprintf(why, sizeof(why), "expected 'dummy' got '%s'", nm ? nm : "(null)");
        mark_fail(TEST_A, why);
    }

    // finish
    (void)plugin_place_work("<END>");
    (void)plugin_wait_finished();
    const char* e3 = plugin_fini();
    if (e3 != NULL) {
        mark_fail(TEST_B, "plugin_fini failed");
        return;
    }
    const char* nm2 = plugin_get_name();
    if (nm2 && strcmp(nm2, "unknown") == 0) {
        mark_pass(TEST_B);
    } else {
        char why[256];
        snprintf(why, sizeof(why), "expected 'unknown' got '%s'", nm2 ? nm2 : "(null)");
        mark_fail(TEST_B, why);
    }
}

// ---------------------------------------------------------------------
// ====== Test 4: common_plugin_init(process, name, queue_size) ======

static void test_common_plugin_init_happy_path(void) {
    const char* TEST = "common_plugin_init: happy path";
    const char* err = common_plugin_init(dummy_process_return_same, "p", 2);
    if (err == NULL) {
        mark_pass(TEST);
    } else {
        mark_fail(TEST, err);
        return;
    }
    finish_if_initialized();
}

static void test_common_plugin_init_invalid_process(void) {
    const char* TEST = "common_plugin_init: invalid process (NULL)";
    const char* err = common_plugin_init(NULL, "p", 2);
    if (err != NULL) {
        mark_pass(TEST);
    } else {
        mark_fail(TEST, "expected error when process_function is NULL");
        finish_if_initialized();
        return;
    }
    // no init occurred, nothing to finish
}

static void test_common_plugin_init_invalid_name(void) {
    const char* TEST1 = "common_plugin_init: invalid name (NULL)";
    const char* TEST2 = "common_plugin_init: invalid name (empty)";

    const char* err1 = common_plugin_init(dummy_process_return_same, NULL, 2);
    if (err1 != NULL) mark_pass(TEST1); else { mark_fail(TEST1, "expected error"); finish_if_initialized(); return; }

    const char* err2 = common_plugin_init(dummy_process_return_same, "", 2);
    if (err2 != NULL) mark_pass(TEST2); else { mark_fail(TEST2, "expected error"); finish_if_initialized(); return; }
}

static void test_common_plugin_init_invalid_queue_size(void) {
    const char* TEST = "common_plugin_init: invalid queue size (<=0)";
    const char* err = common_plugin_init(dummy_process_return_same, "p", 0);
    if (err != NULL) {
        mark_pass(TEST);
    } else {
        mark_fail(TEST, "expected error when queue_size<=0");
        finish_if_initialized();
        return;
    }
}

static void test_common_plugin_init_double_init(void) {
    const char* TEST = "common_plugin_init: double init rejected";

    const char* err1 = common_plugin_init(dummy_process_return_same, "p", 2);
    if (err1 != NULL) { mark_fail(TEST, "first init failed"); return; }

    const char* err2 = common_plugin_init(dummy_process_return_same, "p", 2);
    if (err2 != NULL) {
        mark_pass(TEST);
    } else {
        mark_fail(TEST, "expected error on second init");
    }

    finish_if_initialized();
}

// ---------------------------------------------------------------------
// ====== Test 5: plugin_init(int queue_size) (wrapper) ======

static void test_plugin_init_happy_path(void) {
    const char* TEST = "plugin_init: happy path";
    const char* err = plugin_init(2);
    if (err == NULL) mark_pass(TEST); else { mark_fail(TEST, err); return; }
    finish_if_initialized();
}

static void test_plugin_init_invalid_queue_size(void) {
    const char* TEST = "plugin_init: invalid queue size (<=0)";
    const char* err = plugin_init(0);
    if (err != NULL) mark_pass(TEST); else { mark_fail(TEST, "expected error"); finish_if_initialized(); return; }
    // no init occurred
}

static void test_plugin_init_double_init(void) {
    const char* TEST = "plugin_init: double init rejected";
    const char* err1 = plugin_init(2);
    if (err1 != NULL) { mark_fail(TEST, "first init failed"); return; }
    const char* err2 = plugin_init(2);
    if (err2 != NULL) mark_pass(TEST); else mark_fail(TEST, "expected error on second init");
    finish_if_initialized();
}

// ---------------------------------------------------------------------
// ====== Test 6: plugin_attach(const char* (*next)(const char*)) ======

static void test_plugin_attach_before_init(void) {
    const char* TEST = "plugin_attach: called before init";
    capture_t cap;
    if (start_capture_stream(stderr, &cap, "stderr_attach_before_init.txt") != 0) {
        mark_fail(TEST, "failed to start stderr capture");
        return;
    }

    // Not initialized
    g_spy1_calls = g_spy2_calls = 0;
    plugin_attach(next_place_work_spy1);

    char* out = NULL;
    if (stop_capture_stream(stderr, &cap, &out) != 0) {
        mark_fail(TEST, "failed to stop stderr capture");
        return;
    }

    // Expect some error line; we look for substring to be robust
    if (out && strstr(out, "attach") && strstr(out, "before") ) {
        mark_pass(TEST);
    } else {
        mark_fail(TEST, out ? out : "(no stderr output)");
    }
    free(out);
}

static void test_plugin_attach_ok(void) {
    const char* TEST = "plugin_attach: happy path stores next";
    const char* err = plugin_init(2);
    if (err != NULL) { mark_fail(TEST, "init failed"); return; }

    g_spy1_calls = g_spy2_calls = 0;
    plugin_attach(next_place_work_spy1);

    // Send one item + END to make sure forwarding occurs to spy1
    (void)plugin_place_work("abc");
    (void)plugin_place_work("<END>");
    (void)plugin_wait_finished();
    (void)plugin_fini();

    if (g_spy1_calls >= 1) mark_pass(TEST);
    else mark_fail(TEST, "spy1 was not called");
}

static void test_plugin_attach_double_then_forwarding_unaffected(void) {
    const char* TEST = "plugin_attach: double attach is rejected; wiring unchanged";

    const char* err = plugin_init(2);
    if (err != NULL) { mark_fail(TEST, "init failed"); return; }

    g_spy1_calls = g_spy2_calls = 0;
    plugin_attach(next_place_work_spy1);

    // Second attach attempt should be rejected and not replace wiring
    capture_t cap;
    if (start_capture_stream(stderr, &cap, "stderr_attach_twice.txt") != 0) {
        mark_fail(TEST, "failed to start stderr capture");
        finish_if_initialized();
        return;
    }
    plugin_attach(next_place_work_spy2);
    char* out = NULL;
    (void)stop_capture_stream(stderr, &cap, &out);

    // Forward one item and END
    (void)plugin_place_work("abc");
    (void)plugin_place_work("<END>");
    (void)plugin_wait_finished();
    (void)plugin_fini();

    int ok_msg = (out && strstr(out, "attach") && strstr(out, "twice"));
    free(out);

    if (ok_msg && g_spy1_calls >= 1 && g_spy2_calls == 0) {
        mark_pass(TEST);
    } else {
        char why[128];
        snprintf(why, sizeof(why), "msg:%s spy1:%d spy2:%d",
                 ok_msg ? "ok" : "missing", g_spy1_calls, g_spy2_calls);
        mark_fail(TEST, why);
    }
}

static void test_plugin_attach_after_finish(void) {
    const char* TEST = "plugin_attach: attach after finish rejected";

    const char* err = plugin_init(2);
    if (err != NULL) { mark_fail(TEST, "init failed"); return; }

    plugin_attach(next_place_work_spy1);
    // Finish
    (void)plugin_place_work("<END>");
    (void)plugin_wait_finished();

    // Attempt attach after finish
    capture_t cap;
    if (start_capture_stream(stderr, &cap, "stderr_attach_after_finish.txt") != 0) {
        mark_fail(TEST, "failed to start stderr capture");
        (void)plugin_fini();
        return;
    }
    plugin_attach(next_place_work_spy1);
    char* out = NULL;
    (void)stop_capture_stream(stderr, &cap, &out);

    const char* e3 = plugin_fini();
    if (e3 != NULL) {
        free(out);
        mark_fail(TEST, "fini failed");
        return;
    }

    if (out && strstr(out, "attach") && strstr(out, "finish")) {
        mark_pass(TEST);
    } else {
        mark_fail(TEST, out ? out : "(no stderr output)");
    }
    free(out);
}

// ====== Test 7: plugin_place_work(const char* str) ======

static void test_plugin_place_work_before_init_returns_error(void) {
    const char* TEST = "plugin_place_work: before init returns error";
    (void)plugin_fini(); // ensure not initialized (ignore error)
    const char* err = plugin_place_work("x");
    if (err != NULL) mark_pass(TEST); else mark_fail(TEST, "expected non-NULL error");
}

static void test_plugin_place_work_null_input_returns_error(void) {
    const char* TEST = "plugin_place_work: NULL input returns error";
    const char* e1 = plugin_init(2);
    if (e1 != NULL) { mark_fail(TEST, "init failed"); return; }
    const char* err = plugin_place_work(NULL);
    // cleanup
    (void)plugin_place_work("<END>");
    (void)plugin_wait_finished();
    (void)plugin_fini();

    if (err != NULL) mark_pass(TEST); else mark_fail(TEST, "expected non-NULL error");
}

static void test_plugin_place_work_regular_and_forwarded(void) {
    const char* TEST = "plugin_place_work: regular item forwarded downstream";
    const char* e1 = plugin_init(2);
    if (e1 != NULL) { mark_fail(TEST, "init failed"); return; }

    g_spy1_calls = 0;
    plugin_attach(next_place_work_spy1);

    const char* e2 = plugin_place_work("abc");
    (void)plugin_place_work("<END>");
    (void)plugin_wait_finished();
    (void)plugin_fini();

    if (e2 == NULL && g_spy1_calls >= 1) mark_pass(TEST);
    else mark_fail(TEST, "expected success and at least one spy call");
}

static void test_plugin_place_work_end_enqueued_and_forwarded_once(void) {
    const char* TEST = "plugin_place_work: <END> enqueued; consumer forwards exactly once";
    const char* e1 = common_plugin_init(dummy_process_counting_same, "p", 2);
    if (e1 != NULL) { mark_fail(TEST, "init failed"); return; }

    g_process_calls = 0;
    g_spy_end_calls = 0;
    free(g_spy_last_copied); g_spy_last_copied = NULL;
    plugin_attach(next_place_work_spy_capture);

    const char* e2 = plugin_place_work("<END>");
    (void)plugin_wait_finished();
    (void)plugin_fini();

    int end_seen   = (g_spy_last_copied && strcmp(g_spy_last_copied, "<END>") == 0);
    int proc_zero  = (g_process_calls == 0);
    if (e2 == NULL && g_spy_end_calls == 1 && end_seen && proc_zero) {
        mark_pass(TEST);
    } else {
        char why[160];
        snprintf(why, sizeof(why), "ret:%s spy_calls:%d end_seen:%d proc_calls:%d",
                 e2 ? e2 : "NULL", g_spy_end_calls, end_seen, g_process_calls);
        mark_fail(TEST, why);
    }
}

static void test_plugin_place_work_after_finish_returns_error(void) {
    const char* TEST = "plugin_place_work: after finish returns error";
    const char* e1 = plugin_init(2);
    if (e1 != NULL) { mark_fail(TEST, "init failed"); return; }
    (void)plugin_place_work("<END>");
    (void)plugin_wait_finished();

    const char* err = plugin_place_work("x");
    (void)plugin_fini();

    if (err != NULL) mark_pass(TEST); else mark_fail(TEST, "expected error after finish");
}

// ====== Test 8: plugin_wait_finished(void) ======

static void test_plugin_wait_finished_before_init_returns_error(void) {
    const char* TEST = "plugin_wait_finished: before init returns error";
    (void)plugin_fini(); // ensure not initialized
    const char* err = plugin_wait_finished();
    if (err != NULL) mark_pass(TEST); else mark_fail(TEST, "expected non-NULL error");
}

static void test_plugin_wait_finished_blocks_then_releases_on_end(void) {
    const char* TEST = "plugin_wait_finished: blocks then releases on <END>";
    const char* e1 = plugin_init(2);
    if (e1 != NULL) { mark_fail(TEST, "init failed"); return; }

    wait_ctx_t w = { .done = 0, .err = NULL };
    pthread_t th;
    if (pthread_create(&th, NULL, wait_thread, &w) != 0) {
        (void)plugin_fini();
        mark_fail(TEST, "pthread_create failed");
        return;
    }

    // Give the waiter a moment to block
    sleep_ms(100);
    if (w.done != 0) {
        // It shouldn't have finished yet
        (void)plugin_fini();
        mark_fail(TEST, "wait_finished returned too early");
        return;
    }

    (void)plugin_place_work("<END>");
    pthread_join(th, NULL);

    const char* e2 = plugin_fini();
    if (w.done == 1 && w.err == NULL && e2 == NULL) {
        mark_pass(TEST);
    } else {
        mark_fail(TEST, "wait did not unblock cleanly on <END>");
    }
}

static void test_plugin_wait_finished_idempotent_after_finish(void) {
    const char* TEST = "plugin_wait_finished: idempotent after finish";
    const char* e1 = plugin_init(2);
    if (e1 != NULL) { mark_fail(TEST, "init failed"); return; }

    (void)plugin_place_work("<END>");
    (void)plugin_wait_finished();
    const char* second = plugin_wait_finished(); // should return immediately with NULL
    const char* e2 = plugin_fini();

    if (second == NULL && e2 == NULL) mark_pass(TEST);
    else mark_fail(TEST, "second wait or fini failed");
}

// ====== Test 9: plugin_fini(void) ======

static void test_plugin_fini_before_init_returns_error(void) {
    const char* TEST = "plugin_fini: before init returns error";
    (void)plugin_fini(); // ensure not initialized
    const char* err = plugin_fini();
    if (err != NULL) mark_pass(TEST); else mark_fail(TEST, "expected non-NULL error");
}

static void test_plugin_fini_happy_path(void) {
    const char* TEST = "plugin_fini: happy path after <END>";
    const char* e1 = plugin_init(2);
    if (e1 != NULL) { mark_fail(TEST, "init failed"); return; }

    (void)plugin_place_work("x");
    (void)plugin_place_work("<END>");
    (void)plugin_wait_finished();
    const char* e2 = plugin_fini();

    if (e2 == NULL) mark_pass(TEST);
    else mark_fail(TEST, e2);
}

static void test_plugin_fini_double_call_returns_error(void) {
    const char* TEST = "plugin_fini: double call returns error";
    const char* e1 = plugin_init(2);
    if (e1 != NULL) { mark_fail(TEST, "init failed"); return; }

    (void)plugin_place_work("<END>");
    (void)plugin_wait_finished();
    const char* e2 = plugin_fini();
    const char* e3 = plugin_fini();

    if (e2 == NULL && e3 != NULL) mark_pass(TEST);
    else mark_fail(TEST, "expected second fini to return error");
}

// ====== Test 10: plugin_consumer_thread(void*) as black-box ======

static void test_consumer_end_first_no_processing_and_forwarded(void) {
    const char* TEST = "consumer: <END> first -> no processing; forwarded once";
    const char* e1 = common_plugin_init(dummy_process_counting_same, "p", 2);
    if (e1 != NULL) { mark_fail(TEST, "init failed"); return; }
    g_process_calls = 0;
    g_spy_end_calls = 0;
    free(g_spy_last_copied); g_spy_last_copied = NULL;
    plugin_attach(next_place_work_spy_capture);

    (void)plugin_place_work("<END>");
    (void)plugin_wait_finished();
    (void)plugin_fini();

    int end_seen = (g_spy_last_copied && strcmp(g_spy_last_copied, "<END>") == 0);
    if (g_process_calls == 0 && g_spy_end_calls == 1 && end_seen) {
        mark_pass(TEST);
    } else {
        char why[160];
        snprintf(why, sizeof(why), "proc:%d spy_calls:%d end_seen:%d",
                 g_process_calls, g_spy_end_calls, end_seen);
        mark_fail(TEST, why);
    }
}

static void test_consumer_no_stdout_when_last_plugin(void) {
    const char* TEST = "consumer: no printing in common when last plugin";
    capture_t cap_out;

    // Capture stdout to verify no output from common
    if (start_capture_stream(stdout, &cap_out, "stdout_consumer_last.txt") != 0) {
        mark_fail(TEST, "failed to capture stdout");
        return;
    }

    // const char* e1 = common_plugin_init(dummy_process_counting_same, "p", 2);
    const char* e1 = common_plugin_init(dummy_process_counting_new, "p", 2);
    if (e1 != NULL) {
        (void)stop_capture_stream(stdout, &cap_out, NULL);
        mark_fail(TEST, "init failed");
        return;
    }

    // Do NOT attach -> this plugin is the last; push regular text + END
    (void)plugin_place_work("hello");
    (void)plugin_place_work("<END>");
    (void)plugin_wait_finished();
    (void)plugin_fini();

    char* out = NULL;
    (void)stop_capture_stream(stdout, &cap_out, &out);

    // Expect no stdout output from common layer
    int ok = (out == NULL || out[0] == '\0');
    free(out);
    if (ok) mark_pass(TEST);
    else    mark_fail(TEST, "common printed to stdout unexpectedly");
}

// ---------------------------------------------------------------------
// ====== Main runner ======
int main(void) {
    // ---- Test 1: log_error ----
    test_log_error_basic_format();
    test_log_error_null_context();
    test_log_error_null_message();

    // ---- Test 2: log_info ----
    test_log_info_basic_format();
    test_log_info_null_context();
    test_log_info_null_message();

    // ---- Test 3: plugin_get_name ----
    test_plugin_get_name_before_init();
    test_plugin_get_name_after_init_and_after_fini();

    // ---- Test 4: common_plugin_init ----
    test_common_plugin_init_happy_path();
    test_common_plugin_init_invalid_process();
    test_common_plugin_init_invalid_name();
    test_common_plugin_init_invalid_queue_size();
    test_common_plugin_init_double_init();

    // ---- Test 5: plugin_init (wrapper) ----
    test_plugin_init_happy_path();
    test_plugin_init_invalid_queue_size();
    test_plugin_init_double_init();

    // ---- Test 6: plugin_attach ----
    test_plugin_attach_before_init();
    test_plugin_attach_ok();
    test_plugin_attach_double_then_forwarding_unaffected();
    test_plugin_attach_after_finish();

        // ---- Test 7: plugin_place_work ----
    test_plugin_place_work_before_init_returns_error();
    test_plugin_place_work_null_input_returns_error();
    test_plugin_place_work_regular_and_forwarded();
    test_plugin_place_work_end_enqueued_and_forwarded_once();
    test_plugin_place_work_after_finish_returns_error();

    // ---- Test 8: plugin_wait_finished ----
    test_plugin_wait_finished_before_init_returns_error();
    test_plugin_wait_finished_blocks_then_releases_on_end();
    test_plugin_wait_finished_idempotent_after_finish();

    // ---- Test 9: plugin_fini ----
    test_plugin_fini_before_init_returns_error();
    test_plugin_fini_happy_path();
    test_plugin_fini_double_call_returns_error();

    // ---- Test 10: plugin_consumer_thread (black-box) ----
    test_consumer_end_first_no_processing_and_forwarded();
    test_consumer_no_stdout_when_last_plugin();

    // Summary
    fprintf(stdout, "\n");
    if (g_tests_failed == 0) {
        fprintf(stdout, GREEN "All %d tests passed successfully.\n" NC, g_tests_run);
        return 0;
    } else {
        fprintf(stdout, RED "%d/%d tests failed.\n" NC, g_tests_failed, g_tests_run);
        return 1;
    }
}
