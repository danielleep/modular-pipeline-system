// operating systems/tests/plugin_tests/input_and_flow_tests.c
// NOTE: All comments are in English only, per project requirement.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// ----------------------
// ANSI colors for pass/fail reporting
// ----------------------
#define CLR_RESET   "\x1b[0m"
#define CLR_RED     "\x1b[31m"
#define CLR_GREEN   "\x1b[32m"
#define CLR_BOLD    "\x1b[1m"

// ----------------------
// Configuration & Defaults
// ----------------------


#ifndef DEFAULT_PLUGIN1_PATH
#define DEFAULT_PLUGIN1_PATH "../../plugins/output/uppercaser.so"
#endif

#ifndef DEFAULT_PLUGIN2_PATH
#define DEFAULT_PLUGIN2_PATH "../../plugins/output/logger.so"
#endif


// Special token that signals end-of-stream to the first plugin in the chain.
// Adjust this if your SDK uses a different mechanism to indicate termination.
static const char *kEndToken = "<END>";

// Expected output for Test #2 (empty input line goes through logger at the end).
// The expected line is: "[logger] " followed by a single newline.
static const char *kExpectedTest2Stdout = "[logger] \n";

// ----------------------
// Plugin SDK typedefs (adjust if your SDK signatures differ)
// ----------------------

typedef struct Plugin {
    void *dl;                // dlopen handle
    void *ctx;               // plugin context pointer
    // Function pointers resolved via dlsym:
    int  (*init)(void **plugin_ctx /*, optional params */);
    int  (*attach)(void *plugin_ctx,
                   int (*next_place_work)(void *next_ctx, const char *data),
                   void *next_ctx);
    int  (*place_work)(void *plugin_ctx, const char *data);
    int  (*wait_finished)(void *plugin_ctx);
    void (*fini)(void *plugin_ctx);
} Plugin;

// Symbol names per project spec:
static const char *SYM_INIT          = "plugin_init";
static const char *SYM_ATTACH        = "plugin_attach";
static const char *SYM_PLACE_WORK    = "plugin_place_work";
static const char *SYM_WAIT_FINISHED = "plugin_wait_finished";
static const char *SYM_FINI          = "plugin_fini";

// ----------------------
// Utility: minimal assert helpers
// ----------------------

static void die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[FATAL] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(2);
}

static void warnx(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[WARN] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

// ----------------------
// Utility: load/unload plugin and resolve symbols
// ----------------------

static void plugin_load(Plugin *p, const char *path) {
    memset(p, 0, sizeof(*p));
    p->dl = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!p->dl) {
        die("dlopen failed for '%s': %s", path, dlerror());
    }

    *(void**)(&p->init)          = dlsym(p->dl, SYM_INIT);
    *(void**)(&p->attach)        = dlsym(p->dl, SYM_ATTACH);
    *(void**)(&p->place_work)    = dlsym(p->dl, SYM_PLACE_WORK);
    *(void**)(&p->wait_finished) = dlsym(p->dl, SYM_WAIT_FINISHED);
    *(void**)(&p->fini)          = dlsym(p->dl, SYM_FINI);

    if (!p->init || !p->attach || !p->place_work || !p->wait_finished || !p->fini) {
        die("dlsym missing required symbol(s) in '%s'", path);
    }

    // NOTE: If your SDK requires passing parameters (e.g., queue size),
    // add them here and adapt the init signature accordingly.
    if (p->init(&p->ctx) != 0) {
        die("plugin_init failed for '%s'", path);
    }
}

static void plugin_unload(Plugin *p) {
    if (p->ctx && p->fini) {
        p->fini(p->ctx);
        p->ctx = NULL;
    }
    if (p->dl) {
        dlclose(p->dl);
        p->dl = NULL;
    }
    memset(p, 0, sizeof(*p));
}

// ----------------------
// Utility: connect plugin A -> plugin B
// ----------------------

static void plugin_chain_attach(Plugin *a, Plugin *b) {
    // Attach A so its "next" is B's place_work and context.
    if (a->attach(a->ctx, b->place_work, b->ctx) != 0) {
        die("plugin_attach failed (A -> B)");
    }
    // Attach B as terminal (no next). If your SDK allows NULL next, pass NULLs:
    if (b->attach(b->ctx, NULL, NULL) != 0) {
        die("plugin_attach failed for terminal plugin (B)");
    }
}

// ----------------------
// Utility: capture stdout/stderr while running a critical section
// ----------------------

typedef struct Capture {
    char *out_buf;   // captured stdout (NUL-terminated string)
    size_t out_len;
    char *err_buf;   // captured stderr (NUL-terminated string)
    size_t err_len;
} Capture;

static void free_capture(Capture *c) {
    free(c->out_buf);
    free(c->err_buf);
    memset(c, 0, sizeof(*c));
}

// Read all available data from fd into heap buffer.
static char *read_all_from_fd(int fd, size_t *out_len) {
    const size_t CHUNK = 4096;
    size_t cap = CHUNK;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;

    for (;;) {
        if (len + CHUNK > cap) {
            cap *= 2;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        ssize_t n = read(fd, buf + len, CHUNK);
        if (n < 0) {
            if (errno == EINTR) continue;
            free(buf);
            return NULL;
        }
        if (n == 0) break; // EOF
        len += (size_t)n;
    }
    // NUL-terminate
    char *nb = (char *)realloc(buf, len + 1);
    if (!nb) { free(buf); return NULL; }
    nb[len] = '\0';
    if (out_len) *out_len = len;
    return nb;
}

// Run a function while redirecting stdout/stderr to pipes.
// The run_fn must perform the pipeline operations (place_work, END, wait_finished).
typedef void (*run_section_fn)(void *opaque);

static void with_captured_stdio(run_section_fn run, void *opaque, Capture *cap) {
    memset(cap, 0, sizeof(*cap));

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        die("pipe failed: %s", strerror(errno));
    }

    int saved_stdout = dup(STDOUT_FILENO);
    int saved_stderr = dup(STDERR_FILENO);
    if (saved_stdout < 0 || saved_stderr < 0) {
        die("dup failed: %s", strerror(errno));
    }

    // Redirect stdout/stderr to our pipes.
    if (dup2(out_pipe[1], STDOUT_FILENO) < 0) die("dup2 stdout failed: %s", strerror(errno));
    if (dup2(err_pipe[1], STDERR_FILENO) < 0) die("dup2 stderr failed: %s", strerror(errno));

    // Close write ends on parent side after dup'ing them.
    close(out_pipe[1]);
    close(err_pipe[1]);

    // Execute the critical section.
    run(opaque);

    // Restore stdio.
    fflush(stdout);
    fflush(stderr);
    if (dup2(saved_stdout, STDOUT_FILENO) < 0) die("restore stdout failed: %s", strerror(errno));
    if (dup2(saved_stderr, STDERR_FILENO) < 0) die("restore stderr failed: %s", strerror(errno));
    close(saved_stdout);
    close(saved_stderr);

    // Read captured outputs.
    cap->out_buf = read_all_from_fd(out_pipe[0], &cap->out_len);
    cap->err_buf = read_all_from_fd(err_pipe[0], &cap->err_len);
    close(out_pipe[0]);
    close(err_pipe[0]);

    if (!cap->out_buf || !cap->err_buf) {
        free_capture(cap);
        die("failed to capture stdio");
    }
}

// ----------------------
// Test fixtures
// ----------------------

typedef struct Chain {
    Plugin p1;  // first plugin in chain (e.g., uppercaser)
    Plugin p2;  // terminal plugin (e.g., logger)
} Chain;

static void chain_setup(Chain *c, const char *p1_path, const char *p2_path) {
    plugin_load(&c->p1, p1_path);
    plugin_load(&c->p2, p2_path);
    plugin_chain_attach(&c->p1, &c->p2);
}

static void chain_teardown(Chain *c) {
    // Ensure graceful stop even if not already done
    if (c->p1.wait_finished && c->p1.ctx) c->p1.wait_finished(c->p1.ctx);
    if (c->p2.wait_finished && c->p2.ctx) c->p2.wait_finished(c->p2.ctx);
    plugin_unload(&c->p2);
    plugin_unload(&c->p1);
    memset(c, 0, sizeof(*c));
}

// ----------------------
// Test 1: Empty input, only <END>
// ----------------------

typedef struct {
    Chain *chain;
} Test1Ctx;

static void test1_run_section(void *opaque) {
    Test1Ctx *t = (Test1Ctx *)opaque;

    // No regular input is placed.
    // Signal end-of-stream to the first plugin.
    if (t->chain->p1.place_work(t->chain->p1.ctx, kEndToken) != 0) {
        warnx("Test1: place_work(<END>) failed");
    }

    // Wait for all stages to finish draining.
    (void)t->chain->p2.wait_finished(t->chain->p2.ctx);
    (void)t->chain->p1.wait_finished(t->chain->p1.ctx);
}

static int test1_empty_input_only_end(Chain *chain) {
    Test1Ctx ctx = {.chain = chain};
    Capture cap;
    with_captured_stdio(test1_run_section, &ctx, &cap);

    int failed = 0;

    // STDOUT must be empty for this test.
    if (cap.out_len != 0) {
        fprintf(stderr,
                "[Test1] Expected empty stdout, got %zu bytes: \"%s\"\n",
                cap.out_len, cap.out_buf);
        failed = 1;
    }

    // STDERR must be empty (no errors/warnings).
    if (cap.err_len != 0) {
        fprintf(stderr,
                "[Test1] Expected empty stderr, got %zu bytes: \"%s\"\n",
                cap.err_len, cap.err_buf);
        failed = 1;
    }

    free_capture(&cap);
    return failed ? 1 : 0;
}

// ----------------------
// Test 2: Single empty line, then <END>
// ----------------------

typedef struct {
    Chain *chain;
} Test2Ctx;

static void test2_run_section(void *opaque) {
    Test2Ctx *t = (Test2Ctx *)opaque;

    // Place a single empty string as input (normalized without trailing '\n').
    if (t->chain->p1.place_work(t->chain->p1.ctx, "") != 0) {
        warnx("Test2: place_work(\"\") failed");
    }

    // Signal end-of-stream.
    if (t->chain->p1.place_work(t->chain->p1.ctx, kEndToken) != 0) {
        warnx("Test2: place_work(<END>) failed");
    }

    // Wait for all stages to finish draining.
    (void)t->chain->p2.wait_finished(t->chain->p2.ctx);
    (void)t->chain->p1.wait_finished(t->chain->p1.ctx);
}

static int test2_single_empty_line_then_end(Chain *chain) {
    Test2Ctx ctx = {.chain = chain};
    Capture cap;
    with_captured_stdio(test2_run_section, &ctx, &cap);

    int failed = 0;

    // STDOUT must match exactly: "[logger] \n"
    if (cap.out_len != strlen(kExpectedTest2Stdout) ||
        strcmp(cap.out_buf, kExpectedTest2Stdout) != 0) {
        fprintf(stderr,
                "[Test2] Unexpected stdout.\nExpected (%zu bytes): \"%s\"\nGot (%zu bytes): \"%s\"\n",
                strlen(kExpectedTest2Stdout), kExpectedTest2Stdout,
                cap.out_len, cap.out_buf);
        failed = 1;
    }

    // STDERR must be empty (no errors/warnings).
    if (cap.err_len != 0) {
        fprintf(stderr,
                "[Test2] Expected empty stderr, got %zu bytes: \"%s\"\n",
                cap.err_len, cap.err_buf);
        failed = 1;
    }

    free_capture(&cap);
    return failed ? 1 : 0;
}

// ----------------------
// Main: orchestrate both tests with colored PASS/FAIL prints
// ----------------------

int main(void) {
    // Resolve plugin paths from environment or fall back to defaults.
    const char *p1_path = getenv("PLUGIN1_PATH");
    const char *p2_path = getenv("PLUGIN2_PATH");
    if (!p1_path) p1_path = DEFAULT_PLUGIN1_PATH;
    if (!p2_path) p2_path = DEFAULT_PLUGIN2_PATH;

    fprintf(stderr, "[INFO] Using plugins:\n  P1: %s\n  P2: %s\n", p1_path, p2_path);

    int overall_failures = 0;

    Chain chain;
    memset(&chain, 0, sizeof(chain));

    // Setup chain once for both tests (you may isolate per test if desired).
    chain_setup(&chain, p1_path, p2_path);

    // Run Test 1
    {
        int rc = test1_empty_input_only_end(&chain);
        if (rc != 0) {
            fprintf(stderr, "%s[Test 1]%s %sFAIL%s\n",
                    CLR_BOLD, CLR_RESET, CLR_RED, CLR_RESET);
            overall_failures++;
        } else {
            fprintf(stderr, "%s[Test 1]%s %sPASS%s\n",
                    CLR_BOLD, CLR_RESET, CLR_GREEN, CLR_RESET);
        }
    }

    // Run Test 2
    {
        int rc = test2_single_empty_line_then_end(&chain);
        if (rc != 0) {
            fprintf(stderr, "%s[Test 2]%s %sFAIL%s\n",
                    CLR_BOLD, CLR_RESET, CLR_RED, CLR_RESET);
            overall_failures++;
        } else {
            fprintf(stderr, "%s[Test 2]%s %sPASS%s\n",
                    CLR_BOLD, CLR_RESET, CLR_GREEN, CLR_RESET);
        }
    }

    // Teardown chain once after all tests above.
    chain_teardown(&chain);

    // Summary with colors
    if (overall_failures != 0) {
        fprintf(stderr, "%s[SUMMARY]%s %s%d test(s) failed%s\n",
                CLR_BOLD, CLR_RESET, CLR_RED, overall_failures, CLR_RESET);
        return 1;
    }
    fprintf(stderr, "%s[SUMMARY]%s %sAll tests passed%s\n",
            CLR_BOLD, CLR_RESET, CLR_GREEN, CLR_RESET);
    return 0;
}
