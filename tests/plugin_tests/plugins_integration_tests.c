// tests/integration/plugins_integration_tests.c
// Integration tests (parts A+B) for the modular pipeline system.
// Runs the real binary (./output/analyzer) end-to-end with actual plugins.
// - Sends input via child stdin
// - Captures child's stdout/stderr
// - Verifies exact stdout for deterministic pipelines (no typewriter mixing)
// - Prints PASS/FAIL in colors and a final summary.

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>

/* ---------- Colors ---------- */
#define GREEN  "\033[0;32m"
#define RED    "\033[0;31m"
#define YELLOW "\033[1;33m"
#define NC     "\033[0m"

/* ---------- Test result aggregation ---------- */
static int tests_failed = 0;

static void report_test(const char* name, int ok, const char* extra) {
    if (ok) {
        fprintf(stderr, "%s[PASS]%s %s\n", GREEN, NC, name);
    } else {
        fprintf(stderr, "%s[FAIL]%s %s", RED, NC, name);
        if (extra && *extra) fprintf(stderr, " — %s", extra);
        fputc('\n', stderr);
        tests_failed++;
    }
}

/* ---------- Small dynamic buffer helper ---------- */
typedef struct {
    char* data;
    size_t len;
    size_t cap;
} strbuf;

static void sb_init(strbuf* b) { b->data = NULL; b->len = 0; b->cap = 0; }
static int  sb_reserve(strbuf* b, size_t need) {
    if (need <= b->cap) return 1;
    size_t ncap = b->cap ? b->cap * 2 : 1024;
    while (ncap < need) ncap *= 2;
    char* nb = (char*)realloc(b->data, ncap);
    if (!nb) return 0;
    b->data = nb; b->cap = ncap; return 1;
}
static int  sb_append_data(strbuf* b, const char* p, size_t n) {
    if (!sb_reserve(b, b->len + n + 1)) return 0;
    memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 1;
}
static void sb_free(strbuf* b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }

/* ---------- Core: run analyzer and capture stdout/stderr ---------- */
typedef struct {
    char* out;
    char* err;
    int exit_code;
    int timed_out;
} run_result;

static long long now_ms(void) {
    struct timeval tv; gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

/* Runs ../../output/analyzer from CWD=../../output to ensure plugins can be found.
   queue_size: e.g., "8"
   plugins: array of plugin names (e.g., {"uppercaser","logger"})
   n: number of plugins
   input: string with newlines, include "<END>\n"
   timeout_ms: e.g., 5000
*/
static run_result run_pipeline(const char* queue_size,
                               const char** plugins, int n,
                               const char* input,
                               int timeout_ms)
{
    run_result rr = {0};
    int in_p[2], out_p[2], err_p[2];
    if (pipe(in_p) || pipe(out_p) || pipe(err_p)) {
        rr.exit_code = -1; return rr;
    }

    pid_t pid = fork();
    if (pid < 0) {
        rr.exit_code = -1;
        close(in_p[0]); close(in_p[1]);
        close(out_p[0]); close(out_p[1]);
        close(err_p[0]); close(err_p[1]);
        return rr;
    }

    if (pid == 0) {
        /* Move to project root so relative plugin paths resolve */
        if (chdir("../../") != 0) {
            dprintf(STDERR_FILENO, "chdir(\"../../\") failed: %s\n", strerror(errno));
            _exit(127);
        }

        /* Wire stdin/stdout/stderr to our pipes */
        dup2(in_p[0],  STDIN_FILENO);
        dup2(out_p[1], STDOUT_FILENO);
        dup2(err_p[1], STDERR_FILENO);

        close(in_p[1]);  close(out_p[0]); close(err_p[0]);
        close(in_p[0]);  close(out_p[1]); close(err_p[1]);

        /* Help dlopen find the .so files */
        setenv("LD_LIBRARY_PATH", ".:./output:./plugins", 1);

        /* Build argv for ./output/analyzer <queue> <plugins...> */
        char **argv = (char**)calloc((size_t)n + 3, sizeof(char*));
        if (!argv) _exit(127);
        argv[0] = "./output/analyzer";
        argv[1] = (char*)queue_size;
        for (int i = 0; i < n; ++i) argv[2 + i] = (char*)plugins[i];
        argv[2 + n] = NULL;

        execv(argv[0], argv);
        dprintf(STDERR_FILENO, "execv failed: %s\n", strerror(errno));
        _exit(127);


        dup2(in_p[0],  STDIN_FILENO);
        dup2(out_p[1], STDOUT_FILENO);
        dup2(err_p[1], STDERR_FILENO);

        close(in_p[1]);  close(out_p[0]); close(err_p[0]);
        close(in_p[0]);  close(out_p[1]); close(err_p[1]);
    }

    /* Parent */
    close(in_p[0]);  close(out_p[1]); close(err_p[1]);

    /* Non-blocking reads */
    fcntl(out_p[0], F_SETFL, O_NONBLOCK);
    fcntl(err_p[0], F_SETFL, O_NONBLOCK);

    /* Write input then close stdin to signal EOF */
    if (input && *input) {
        ssize_t off = 0, L = (ssize_t)strlen(input);
        while (off < L) {
            ssize_t w = write(in_p[1], input + off, (size_t)(L - off));
            if (w > 0) off += w;
            else if (w < 0 && errno == EINTR) continue;
            else break;
        }
    }
    close(in_p[1]);

    /* Read until child exits or timeout */
    strbuf out_sb, err_sb; sb_init(&out_sb); sb_init(&err_sb);
    long long deadline = now_ms() + timeout_ms;
    int out_open = 1, err_open = 1;
    int child_done = 0;

    while (!child_done && now_ms() < deadline) {
        fd_set rfds; FD_ZERO(&rfds);
        int maxfd = -1;
        if (out_open) { FD_SET(out_p[0], &rfds); if (out_p[0] > maxfd) maxfd = out_p[0]; }
        if (err_open) { FD_SET(err_p[0], &rfds); if (err_p[0] > maxfd) maxfd = err_p[0]; }

        struct timeval tv;
        tv.tv_sec = 0; tv.tv_usec = 150 * 1000; /* 150ms slice */
        int sel = select(maxfd + 1, &rfds, NULL, NULL, &tv);

        if (sel > 0) {
            char buf[4096];
            if (out_open && FD_ISSET(out_p[0], &rfds)) {
                for (;;) {
                    ssize_t nrd = read(out_p[0], buf, sizeof(buf));
                    if (nrd > 0) { sb_append_data(&out_sb, buf, (size_t)nrd); }
                    else if (nrd == 0) { out_open = 0; close(out_p[0]); break; }
                    else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    else if (errno == EINTR) continue;
                    else { out_open = 0; close(out_p[0]); break; }
                }
            }
            if (err_open && FD_ISSET(err_p[0], &rfds)) {
                for (;;) {
                    ssize_t nrd = read(err_p[0], buf, sizeof(buf));
                    if (nrd > 0) { sb_append_data(&err_sb, buf, (size_t)nrd); }
                    else if (nrd == 0) { err_open = 0; close(err_p[0]); break; }
                    else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    else if (errno == EINTR) continue;
                    else { err_open = 0; close(err_p[0]); break; }
                }
            }
        }

        int status;
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            child_done = 1;
            if (WIFEXITED(status)) rr.exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) rr.exit_code = 128 + WTERMSIG(status);
            else rr.exit_code = -1;
        }
    }

    if (!child_done) {
        rr.timed_out = 1;
        kill(pid, SIGKILL);
        int status; (void)waitpid(pid, &status, 0);
    }

    /* Close remaining FDs if still open */
    if (out_open) close(out_p[0]);
    if (err_open) close(err_p[0]);

    rr.out = out_sb.data ? out_sb.data : strdup("");
    rr.err = err_sb.data ? err_sb.data : strdup("");
    if (!rr.out) rr.out = strdup("");
    if (!rr.err) rr.err = strdup("");
    return rr;
}

static void free_result(run_result* rr) {
    free(rr->out); free(rr->err);
    rr->out = rr->err = NULL;
}

/* ---------- Expect helpers ---------- */
static void expect_exact(const char* name,
                         const char* queue_size,
                         const char** plugins, int n,
                         const char* input,
                         const char* expected_stdout)
{
    run_result rr = run_pipeline(queue_size, plugins, n, input, 5000);
    int ok = (rr.timed_out == 0) &&
             (rr.exit_code == 0) &&
             (strcmp(rr.out, expected_stdout) == 0) &&
             (strlen(rr.err) == 0);
    char extra[256];
    if (!ok) {
        snprintf(extra, sizeof(extra),
                 "exit=%d timeout=%d\nSTDOUT:\n---\n%s---\nSTDERR:\n---\n%s---\n",
                 rr.exit_code, rr.timed_out, rr.out, rr.err);
    }
    report_test(name, ok, ok ? "" : extra);
    free_result(&rr);
}

/* Expect failure: non-zero exit, stderr not empty, and (optionally) contains substring */
static void expect_fail(const char* name,
                        const char* queue_size,
                        const char** plugins, int n,
                        const char* input,
                        const char* stderr_must_contain)
{
    run_result rr = run_pipeline(queue_size, plugins, n, input, 5000);
    int ok = (rr.timed_out == 0) && (rr.exit_code != 0) && (strlen(rr.err) > 0);
    if (ok && stderr_must_contain && *stderr_must_contain) {
        ok = strstr(rr.err, stderr_must_contain) != NULL;
    }
    char extra[256];
    if (!ok) {
        snprintf(extra, sizeof(extra),
                 "exit=%d timeout=%d\nSTDOUT:\n---\n%s---\nSTDERR:\n---\n%s---\n",
                 rr.exit_code, rr.timed_out, rr.out, rr.err);
    }
    report_test(name, ok, ok ? "" : extra);
    free_result(&rr);
}


/* Count non-overlapping occurrences of a substring (for typewriter / mixed output checks) */
static int count_substr(const char* hay, const char* needle) {
    if (!hay || !needle) return 0;
    size_t nlen = strlen(needle);
    if (nlen == 0) return 0;
    int count = 0;
    const char* p = hay;
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += nlen;
    }
    return count;
}

/* ================================================================
   A) Sanity
   ================================================================ */

static void test_A1_logger_only(void) {
    const char* plugins[] = {"logger"};
    const char* input = "hello\n<END>\n";
    const char* expected = "[logger] hello\n";
    expect_exact("A1: logger only", "8", plugins, 1, input, expected);
}

static void test_A2_typewriter_only(void) {
    const char* plugins[] = {"typewriter"};
    const char* input = "Hi\n<END>\n";
    const char* expected = "[typewriter] Hi\n";
    expect_exact("A2: typewriter only", "8", plugins, 1, input, expected);
}

static void test_A3_uppercaser_only(void) {
    const char* plugins[] = {"uppercaser"};
    const char* input = "hello\n<END>\n";
    const char* expected = ""; /* no printing stage */
    expect_exact("A3: uppercaser only (no output)", "8", plugins, 1, input, expected);
}

/* ================================================================
   B) Transform + print (exact)
   ================================================================ */

static void test_B1_uppercaser_logger(void) {
    const char* plugins[] = {"uppercaser", "logger"};
    const char* input = "hello\n<END>\n";
    const char* expected = "[logger] HELLO\n";
    expect_exact("B1: uppercaser -> logger", "8", plugins, 2, input, expected);
}

static void test_B2_flipper_logger(void) {
    const char* plugins[] = {"flipper", "logger"};
    const char* input = "abcd\n<END>\n";
    const char* expected = "[logger] dcba\n";
    expect_exact("B2: flipper -> logger", "8", plugins, 2, input, expected);
}

static void test_B3_rotator_logger(void) {
    const char* plugins[] = {"rotator", "logger"};
    const char* input = "abcd\n<END>\n";
    const char* expected = "[logger] dabc\n";
    expect_exact("B3: rotator -> logger", "8", plugins, 2, input, expected);
}

static void test_B4_expander_logger(void) {
    const char* plugins[] = {"expander", "logger"};
    const char* input = "ABC\n<END>\n";
    const char* expected = "[logger] A B C\n";
    expect_exact("B4: expander -> logger", "8", plugins, 2, input, expected);
}

/* ================================================================
   C) Chains (exact)
   ================================================================ */

static void test_C1_uppercaser_rotator_logger(void) {
    const char* plugins[] = {"uppercaser", "rotator", "logger"};
    const char* input = "hello\n<END>\n";
    const char* expected = "[logger] OHELL\n";
    expect_exact("C1: uppercaser -> rotator -> logger", "8", plugins, 3, input, expected);
}

static void test_C2_rotator_expander_logger(void) {
    const char* plugins[] = {"rotator", "expander", "logger"};
    const char* input = "AB\n<END>\n";
    const char* expected = "[logger] B A\n";  /* AB -> (rot) BA -> (expand) B␠A */
    expect_exact("C2: rotator -> expander -> logger", "8", plugins, 3, input, expected);
}

static void test_C3_expander_rotator_logger(void) {
    const char* plugins[] = {"expander", "rotator", "logger"};
    const char* input = "AB\n<END>\n";
    const char* expected = "[logger] BA \n";  /* AB -> (expand) A␠B -> (rot) BA␠ */
    expect_exact("C3: expander -> rotator -> logger", "8", plugins, 3, input, expected);
}

/* ================================================================
   D) With typewriter (existence checks, not order)
   ================================================================ */

static void test_D1_chain_with_typewriter(void) {
    const char* plugins[] = {"uppercaser", "rotator", "logger", "flipper", "typewriter"};
    const char* input = "hello\n<END>\n";

    run_result rr = run_pipeline("8", plugins, 5, input, 5000);
    int ok = (rr.timed_out == 0) && (rr.exit_code == 0) &&
             (count_substr(rr.out, "[logger] ") >= 1) &&
             (count_substr(rr.out, "[typewriter] ") >= 1);
    /* Note: we intentionally do not assert exact substrings for typewriter text,
       since interleaving is allowed and may break contiguous sequences. */
    report_test("D1: uppercaser -> rotator -> logger -> flipper -> typewriter (existence)", ok, "");
    free_result(&rr);
}

static void test_D2_logger_typewriter_three_lines(void) {
    const char* plugins[] = {"logger", "typewriter"};
    const char* input =
        "a\n"
        "b\n"
        "c\n"
        "<END>\n";

    run_result rr = run_pipeline("8", plugins, 2, input, 5000);
    /* Robust check: just ensure both emitters appeared expected number of times.
       Exact contiguous lines may be interleaved, so we avoid strict equality. */
    int ok = (rr.timed_out == 0) && (rr.exit_code == 0) &&
             (count_substr(rr.out, "[logger] ") == 3) &&
             (count_substr(rr.out, "[typewriter] ") == 3);
    report_test("D2: logger + typewriter with 3 lines (counts only)", ok, "");
    free_result(&rr);
}

/* ================================================================
   E) Multiple lines & backpressure
   ================================================================ */

static void test_E1_cap1_uppercaser_logger_20_lines(void) {
    const char* plugins[] = {"uppercaser", "logger"};
    const int N = 20;

    /* Build input: line00..line19 + <END> */
    strbuf in; sb_init(&in);
    char tmp[64];
    for (int i = 0; i < N; ++i) {
        snprintf(tmp, sizeof(tmp), "line%02d\n", i);
        sb_append_data(&in, tmp, strlen(tmp));
    }
    sb_append_data(&in, "<END>\n", 6);

    /* Build expected stdout: [logger] LINE00..LINE19 */
    strbuf exp; sb_init(&exp);
    for (int i = 0; i < N; ++i) {
        snprintf(tmp, sizeof(tmp), "[logger] LINE%02d\n", i);
        sb_append_data(&exp, tmp, strlen(tmp));
    }

    expect_exact("E1: cap=1, uppercaser->logger, 20 lines (order preserved)",
                 "1", plugins, 2, in.data, exp.data);

    sb_free(&in);
    sb_free(&exp);
}

static void test_E2_cap2_typewriter_many_lines_counts(void) {
    const char* plugins[] = {"typewriter"};
    const int N = 8;

    /* Build input: l0..l7 + <END> */
    strbuf in; sb_init(&in);
    char tmp[32];
    for (int i = 0; i < N; ++i) {
        snprintf(tmp, sizeof(tmp), "l%d\n", i);
        sb_append_data(&in, tmp, strlen(tmp));
    }
    sb_append_data(&in, "<END>\n", 6);

    run_result rr = run_pipeline("2", plugins, 1, in.data, 5000);
    int ok = (rr.timed_out == 0) && (rr.exit_code == 0) &&
             (count_substr(rr.out, "[typewriter] ") == N);
    report_test("E2: cap=2, typewriter only, N lines (counts only)", ok, "");
    free_result(&rr);
    sb_free(&in);
}

/* ================================================================
   F) <END> positioning
   ================================================================ */

static void test_F1_end_only_no_output(void) {
    const char* plugins[] = {"logger"};
    const char* input = "<END>\n";
    const char* expected = "";
    expect_exact("F1: <END> only -> no output, clean exit",
                 "8", plugins, 1, input, expected);
}

static void test_F2_end_in_middle_ignores_rest(void) {
    const char* plugins[] = {"uppercaser", "logger"};
    const char* input =
        "hello\n"
        "<END>\n"
        "world\n"; /* must be ignored */
    const char* expected = "[logger] HELLO\n";
    expect_exact("F2: <END> in middle -> only lines before are processed",
                 "8", plugins, 2, input, expected);
}

/* ================================================================
   G) Duplicate plugins
   ================================================================ */

static void test_G1_upper_upper_logger(void) {
    const char* plugins[] = {"uppercaser", "uppercaser", "logger"};
    const char* input = "hello\n<END>\n";
    const char* expected = "[logger] HELLO\n"; /* same as a single uppercaser */
    expect_exact("G1: uppercaser + uppercaser -> logger", "8", plugins, 3, input, expected);
}

static void test_G2_flipper_flipper_logger(void) {
    const char* plugins[] = {"flipper", "flipper", "logger"};
    const char* input = "abc\n<END>\n";
    const char* expected = "[logger] abc\n"; /* double flip returns original */
    expect_exact("G2: flipper + flipper -> logger", "8", plugins, 3, input, expected);
}

static void test_G3_logger_logger_twice(void) {
    const char* plugins[] = {"logger", "logger"};
    const char* input = "hi\n<END>\n";
    const char* expected =
        "[logger] hi\n"
        "[logger] hi\n"; /* both stages print */
    expect_exact("G3: logger + logger (two prints)", "8", plugins, 2, input, expected);
}

/* ================================================================
   H) Characters & lengths
   ================================================================ */

static void test_H1a_expander_logger_punct_spaces(void) {
    const char* plugins[] = {"expander", "logger"};
    const char* input = "A b! 1\n<END>\n";

    /* Build expected: expander inserts a single space between every adjacent pair.
       "A b! 1" -> "A␠ ␠b␠!␠ ␠1" == "A  b !  1" */
    const char* expected = "[logger] A  b !  1\n";
    expect_exact("H1a: expander -> logger on \"A b! 1\"", "8", plugins, 2, input, expected);
}

static void test_H1b_rotator_logger_punct_spaces(void) {
    const char* plugins[] = {"rotator", "logger"};
    const char* input = "A b! 1\n<END>\n";
    const char* expected = "[logger] 1A b! \n"; /* right-rotate by one */
    expect_exact("H1b: rotator -> logger on \"A b! 1\"", "8", plugins, 2, input, expected);
}

static void test_H2_near_max_expander_logger(void) {
    const char* plugins[] = {"expander", "logger"};
    const size_t LEN = 512; /* near limit; expander output ~ 2*LEN-1 */
    strbuf in; sb_init(&in);
    strbuf expanded; sb_init(&expanded);
    strbuf expected; sb_init(&expected);

    /* Build input payload (LEN chars) + <END> */
    for (size_t i = 0; i < LEN; ++i) {
        char ch;
        switch (i % 4) { case 0: ch = 'a'; break; case 1: ch = 'Z'; break; case 2: ch = '9'; break; default: ch = '#'; }
        sb_append_data(&in, &ch, 1);
        /* Build expanded mirror: char + (space if not last) */
        sb_append_data(&expanded, &ch, 1);
        if (i + 1 < LEN) sb_append_data(&expanded, " ", 1);
    }
    sb_append_data(&in, "\n<END>\n", 7);

    /* Wrap for logger */
    sb_append_data(&expected, "[logger] ", 9);
    sb_append_data(&expected, expanded.data ? expanded.data : "", expanded.len);
    sb_append_data(&expected, "\n", 1);

    expect_exact("H2: expander -> logger, near-max length (512 -> ~1023)",
                 "8", plugins, 2, in.data, expected.data);

    sb_free(&in);
    sb_free(&expanded);
    sb_free(&expected);
}

static void test_H3_long_uppercaser_only_no_output(void) {
    const char* plugins[] = {"uppercaser"};
    const size_t LEN = 1000;
    strbuf in; sb_init(&in);

    for (size_t i = 0; i < LEN; ++i) {
        char ch;
        switch (i % 3) { case 0: ch = 'a'; break; case 1: ch = 'Z'; break; default: ch = '9'; }
        sb_append_data(&in, &ch, 1);
    }
    sb_append_data(&in, "\n<END>\n", 7);

    const char* expected = ""; /* no printing stage */
    expect_exact("H3: uppercaser only, long input, no output", "8", plugins, 1, in.data, expected);

    sb_free(&in);
}

/* ================================================================
   I) Error paths / invalid usage
   ================================================================ */

static void test_I1_plugin_not_found(void) {
    const char* plugins[] = {"uppercaser", "NOTFOUND", "logger"};
    const char* input = "hello\n<END>\n";
    /* We only require non-zero exit and some stderr; best-effort to also see the token "NOTFOUND". */
    expect_fail("I1: plugin not found in chain", "8", plugins, 3, input, "NOTFOUND");
}

static void test_I2_invalid_capacity_zero(void) {
    const char* plugins[] = {"logger"};
    const char* input = "hello\n<END>\n";
    /* Do not assume exact wording; just require failure with non-empty stderr. */
    expect_fail("I2: invalid capacity (0) -> should fail", "0", plugins, 1, input, NULL);
}

/* ================================================================
   J) Stability / no timeouts
   ================================================================ */

static void test_J1_cap1_upper_logger_100_lines_exact(void) {
    const char* plugins[] = {"uppercaser", "logger"};
    const int N = 100;

    strbuf in; sb_init(&in);
    strbuf exp; sb_init(&exp);
    char tmp[64];

    for (int i = 0; i < N; ++i) {
        snprintf(tmp, sizeof(tmp), "line%02d\n", i);
        sb_append_data(&in, tmp, strlen(tmp));
        snprintf(tmp, sizeof(tmp), "[logger] LINE%02d\n", i);
        sb_append_data(&exp, tmp, strlen(tmp));
    }
    sb_append_data(&in, "<END>\n", 6);

    expect_exact("J1: cap=1, uppercaser->logger, 100 lines (no timeout, exact)",
                 "1", plugins, 2, in.data, exp.data);

    sb_free(&in);
    sb_free(&exp);
}

static void test_J2_cap2_rotator_expander_logger_60_lines_counts(void) {
    const char* plugins[] = {"rotator", "expander", "logger"};
    const int N = 60;

    strbuf in; sb_init(&in);
    char tmp[64];
    for (int i = 0; i < N; ++i) {
        snprintf(tmp, sizeof(tmp), "v%02d\n", i);
        sb_append_data(&in, tmp, strlen(tmp));
    }
    sb_append_data(&in, "<END>\n", 6);

    run_result rr = run_pipeline("2", plugins, 3, in.data, 5000);
    int ok = (rr.timed_out == 0) && (rr.exit_code == 0) &&
             (count_substr(rr.out, "[logger] ") == N);
    report_test("J2: cap=2, rotator->expander->logger, 60 lines (no timeout, count)", ok, "");
    free_result(&rr);
    sb_free(&in);
}

static void test_J3_cap1_typewriter_20_lines_counts(void) {
    const char* plugins[] = {"typewriter"};
    const int N = 20; /* ~2s total at 100ms/char per char sequence; safe under 5s */

    strbuf in; sb_init(&in);
    char tmp[32];
    for (int i = 0; i < N; ++i) {
        snprintf(tmp, sizeof(tmp), "t%02d\n", i);
        sb_append_data(&in, tmp, strlen(tmp));
    }
    sb_append_data(&in, "<END>\n", 6);

    run_result rr = run_pipeline("1", plugins, 1, in.data, 5000);
    int ok = (rr.timed_out == 0) && (rr.exit_code == 0) &&
             (count_substr(rr.out, "[typewriter] ") == N);
    report_test("J3: cap=1, typewriter only, 20 lines (no timeout, count)", ok, "");
    free_result(&rr);
    sb_free(&in);
}


/* ---------- Main ---------- */
int main(void) {
    fprintf(stderr, "======== [INTEGRATION TESTS] ========\n");
    fprintf(stderr, "\n");

    /* A) Sanity */
    test_A1_logger_only();
    test_A2_typewriter_only();
    test_A3_uppercaser_only();

    /* B) Transform + print */
    test_B1_uppercaser_logger();
    test_B2_flipper_logger();
    test_B3_rotator_logger();
    test_B4_expander_logger();

    /* C) Chains */
    test_C1_uppercaser_rotator_logger();
    test_C2_rotator_expander_logger();
    test_C3_expander_rotator_logger();

    /* D) With typewriter (existence) */
    test_D1_chain_with_typewriter();
    test_D2_logger_typewriter_three_lines();

    /* E) Multiple lines & backpressure */
    test_E1_cap1_uppercaser_logger_20_lines();
    test_E2_cap2_typewriter_many_lines_counts();

    /* F) <END> positioning */
    test_F1_end_only_no_output();
    test_F2_end_in_middle_ignores_rest();

    /* G) Duplicate plugins */
    test_G1_upper_upper_logger();
    test_G2_flipper_flipper_logger();
    test_G3_logger_logger_twice();

    /* H) Characters & lengths */
    test_H1a_expander_logger_punct_spaces();
    test_H1b_rotator_logger_punct_spaces();
    test_H2_near_max_expander_logger();
    test_H3_long_uppercaser_only_no_output();

    /* I) Error paths / invalid usage */
    test_I1_plugin_not_found();
    test_I2_invalid_capacity_zero();

    /* J) Stability / no timeouts */
    test_J1_cap1_upper_logger_100_lines_exact();
    test_J2_cap2_rotator_expander_logger_60_lines_counts();
    test_J3_cap1_typewriter_20_lines_counts();

    fprintf(stderr, "\n");

    if (tests_failed == 0) {
        fprintf(stderr, "%sAll integration tests passed.%s\n", GREEN, NC);
        return EXIT_SUCCESS;
    } else {
        fprintf(stderr, "%sSome integration tests FAILED (%d).%s\n",
                RED, tests_failed, NC);
        return EXIT_FAILURE;
    }
}
