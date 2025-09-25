// File: plugin_common_integration_tests.c
// Run from: /operating systems/tests/plugin_common
// Purpose: Integration tests 1–3 for plugin_common (with stubs instead of real plugins)

#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>

#include "../../plugins/plugin_common.h"

// ================= Colors & summary =================
#define GREEN  "\033[0;32m"
#define RED    "\033[0;31m"
#define NC     "\033[0m"

static int g_tests_run = 0, g_tests_failed = 0;
static void pass(const char* name){ ++g_tests_run; fprintf(stdout, GREEN "[PASS]" NC " %s\n", name); }
static void fail(const char* name, const char* why){ ++g_tests_run; ++g_tests_failed; fprintf(stdout, RED "[FAIL]" NC " %s: %s\n", name, why); }

// ================= Capture helpers (stdout/stderr) =================
#include <fcntl.h>
#include <unistd.h>

typedef struct { int saved_fd; char path[256]; } cap_t;

static int cap_start(FILE* stream, cap_t* cap, const char* fname){
    if(!cap) return -1;
    snprintf(cap->path, sizeof(cap->path), "%s", fname);
    fflush(stream);
    int fd = fileno(stream);
    cap->saved_fd = dup(fd);
    if(cap->saved_fd < 0) return -1;
    int tmp = open(cap->path, O_CREAT|O_TRUNC|O_RDWR, 0600);
    if(tmp < 0){ close(cap->saved_fd); return -1; }
    if(dup2(tmp, fd) < 0){ close(tmp); close(cap->saved_fd); return -1; }
    close(tmp);
    return 0;
}
static int cap_stop(FILE* stream, cap_t* cap, char** out){
    if(!cap) return -1;
    fflush(stream);
    int fd = fileno(stream);
    if(dup2(cap->saved_fd, fd) < 0){ close(cap->saved_fd); return -1; }
    close(cap->saved_fd);

    FILE* f = fopen(cap->path, "rb");
    if(!f) return -1;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((sz>0?(size_t)sz:0)+1);
    size_t n = (sz>0)?fread(buf,1,(size_t)sz,f):0; buf[n]='\0'; fclose(f);
    if(out) *out = buf; else free(buf);
    unlink(cap->path);
    return 0;
}

// ================= Small utils =================
static int is_end_token(const char* s){ return (s && strcmp(s, "<END>")==0); }

static char* dup_upper(const char* s){
    size_t n = strlen(s);
    char* r = (char*)malloc(n+1);
    if(!r) return NULL;
    for(size_t i=0;i<n;++i) r[i] = (char)toupper((unsigned char)s[i]);
    r[n]='\0';
    return r;
}

// Collection (no-print) for integration checks
static char** g_collect = NULL;
static int g_collect_sz = 0, g_collect_cap = 0;

static void collect_reset(void){
    for (int i = 0; i < g_collect_sz; ++i) free(g_collect[i]);
    free(g_collect);
    g_collect = NULL; g_collect_sz = 0; g_collect_cap = 0;
}
static void collect_push(const char* s){
    if (!s) s = "";
    if (g_collect_sz == g_collect_cap) {
        int nc = g_collect_cap ? g_collect_cap * 2 : 16;
        g_collect = (char**)realloc(g_collect, (size_t)nc * sizeof(*g_collect));
        g_collect_cap = nc;
    }
    g_collect[g_collect_sz++] = strdup(s);
}
static int collect_contains(const char* s){
    for (int i = 0; i < g_collect_sz; ++i) if (strcmp(g_collect[i], s) == 0) return 1;
    return 0;
}

static char* dup_reverse(const char* s){
    size_t n = strlen(s);
    char* r = (char*)malloc(n+1);
    if(!r) return NULL;
    for(size_t i=0;i<n;++i) r[i] = s[n-1-i];
    r[n]='\0';
    return r;
}

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}


// ================= Process functions (stubs) =================
// IT1: first stage – uppercase (returns NEW buffer)
static const char* proc_uppercase(const char* in){
    return dup_upper(in);
}

// IT2: identity (returns SAME pointer) – useful when we only want to pass through
static const char* proc_identity_same(const char* in){
    return in;
}

// Fail on specific pattern; otherwise pass through (return SAME pointer)
static const char* proc_fail_on_pattern(const char* in){
    if (in && strcmp(in, "FAILME") == 0) return NULL;
    return in;
}


// IT3: “logger in the middle” – print intermediate, then pass SAME pointer onward
static const char* proc_logger_like(const char* in){
    fputs(in, stdout); fputc('\n', stdout); fflush(stdout);
    return in;
}

static const char* proc_slow_same(const char* in){
    sleep_ms(5); // simulate slow processing
    return in;
}


// ================= next_place_work stubs =================
// IT1/IT3: last stage – reverse then print; never prints <END>; returns NULL
static const char* next_reverse_and_print(const char* s){
    if(is_end_token(s)) return NULL;
    char* rev = dup_reverse(s);
    if(rev){ fputs(rev, stdout); fputc('\n', stdout); fflush(stdout); free(rev); }
    return NULL;
}

// IT2: counting receiver – never prints; remembers if got <END>
static int g_next_calls = 0;
static char* g_next_last = NULL;
static const char* next_counting_no_print(const char* s){
    ++g_next_calls;
    free(g_next_last); g_next_last = strdup(s?s:"");
    return NULL;
}

static const char* next_collect_no_print(const char* s){
    if (!is_end_token(s)) collect_push(s);
    return NULL;
}

// Return an error string on specific pattern; otherwise collect (no print)
static const char* next_error_on_pattern(const char* s){
    if (s && strcmp(s, "ERRME") == 0) return "downstream error";
    if (!is_end_token(s)) collect_push(s);
    return NULL;
}

// Two-stage chain to count how many times <END> is forwarded through both "stages"
static int g_end_count_stage1 = 0;
static int g_end_count_stage2 = 0;

static const char* next_chain_stage2(const char* s){
    if (is_end_token(s)) g_end_count_stage2++;
    return NULL; // no printing
}

static const char* next_chain_stage1(const char* s){
    if (is_end_token(s)) g_end_count_stage1++;
    // forward to "stage2"
    return next_chain_stage2(s);
}


// ================= Common clean-up =================
static void __attribute__((unused)) finish_all(void){
    (void)plugin_place_work("<END>");
    (void)plugin_wait_finished();
    (void)plugin_fini();
}

// ===================== IT1 =====================
// Chain: [uppercase] -> next(reverse+print). Expect printed: reverse(uppercase(input))
static void it1_chain_end_only(void){
    const char* TEST = "IT1: chain transforms; print only at end";

    cap_t cap; if(cap_start(stdout, &cap, "it1.out")!=0){ fail(TEST,"capture failed"); return; }

    const char* err = common_plugin_init(proc_uppercase, "it1", 2);
    if(err){ cap_stop(stdout,&cap,NULL); fail(TEST, err); return; }
    plugin_attach(next_reverse_and_print);

    const char* inputs[] = {"Hello", "abc", "", "MiXeD"};
    size_t N = sizeof(inputs)/sizeof(inputs[0]);
    for(size_t i=0;i<N;++i) plugin_place_work(inputs[i]);
    plugin_place_work("<END>");
    plugin_wait_finished();
    plugin_fini();

    char* out=NULL; cap_stop(stdout,&cap,&out);

    // Build expected
    char* expected = (char*)calloc(1,1);
    for(size_t i=0;i<N;++i){
        char* u = dup_upper(inputs[i]);
        char* r = dup_reverse(u?u:"");
        size_t old = strlen(expected), add = strlen(r? r:"") + 1;
        expected = (char*)realloc(expected, old+add+1);
        memcpy(expected+old, r?r:"", add-1);
        expected[old+add-1]='\n';
        expected[old+add]='\0';
        free(u); free(r);
    }

    int ok = (strcmp(out?out:"", expected?expected:"")==0);
    if(ok) pass(TEST);
    else {
        char why[256]; snprintf(why,sizeof(why),"mismatch output");
        fail(TEST, why);
    }
    free(out); free(expected);
}

// ===================== IT2 =====================
// Only <END> flows through, no prints, next called exactly once with "<END>"
static void it2_end_flows_not_printed(void){
    const char* TEST = "IT2: <END> flows through; not printed";

    cap_t cap; if(cap_start(stdout,&cap,"it2.out")!=0){ fail(TEST,"capture failed"); return; }

    g_next_calls = 0; free(g_next_last); g_next_last=NULL;

    const char* err = common_plugin_init(proc_identity_same, "it2", 2);
    if(err){ cap_stop(stdout,&cap,NULL); fail(TEST, err); return; }
    plugin_attach(next_counting_no_print);

    plugin_place_work("<END>");
    plugin_wait_finished();
    plugin_fini();

    char* out=NULL; cap_stop(stdout,&cap,&out);

    int ok_stdout_empty = (!out || out[0]=='\0');
    int ok_next_called_once = (g_next_calls==1 && g_next_last && strcmp(g_next_last,"<END>")==0);

    if(ok_stdout_empty && ok_next_called_once) pass(TEST);
    else {
        char why[128];
        snprintf(why,sizeof(why),"stdout:'%s' calls=%d last='%s'",
                 out?out:"", g_next_calls, g_next_last?g_next_last:"(null)");
        fail(TEST, why);
    }
    free(out);
}

// ===================== IT3 =====================
// “logger in middle”: process prints intermediate; next prints final (reverse)
static void it3_logger_middle_intermediate_and_final(void){
    const char* TEST = "IT3: logger-like in middle + final printer";

    cap_t cap; if(cap_start(stdout,&cap,"it3.out")!=0){ fail(TEST,"capture failed"); return; }

    const char* err = common_plugin_init(proc_logger_like, "it3", 2);
    if(err){ cap_stop(stdout,&cap,NULL); fail(TEST, err); return; }
    plugin_attach(next_reverse_and_print);

    const char* inputs[] = {"Aa", "xyz", "END? no", ""};
    size_t N = sizeof(inputs)/sizeof(inputs[0]);
    for(size_t i=0;i<N;++i) plugin_place_work(inputs[i]);
    plugin_place_work("<END>");
    plugin_wait_finished();
    plugin_fini();

    char* out=NULL; cap_stop(stdout,&cap,&out);

    // Expected: for each input -> line of intermediate (same as input), then reversed final
    char* expected = (char*)calloc(1,1);
    for(size_t i=0;i<N;++i){
        // intermediate
        size_t old = strlen(expected), add = strlen(inputs[i]) + 1;
        expected = (char*)realloc(expected, old+add+1);
        memcpy(expected+old, inputs[i], add-1);
        expected[old+add-1]='\n';
        expected[old+add]='\0';
        // final
        char* r = dup_reverse(inputs[i]);
        old = strlen(expected); add = strlen(r) + 1;
        expected = (char*)realloc(expected, old+add+1);
        memcpy(expected+old, r, add-1);
        expected[old+add-1]='\n';
        expected[old+add]='\0';
        free(r);
    }

    int ok = (strcmp(out?out:"", expected?expected:"")==0);
    if(ok) pass(TEST);
    else fail(TEST, "mismatch output");
    free(out); free(expected);
}

// ===================== IT4 =====================

static void it4_no_printer_last_no_stdout(void){
    const char* TEST = "IT4: no printer at end -> STDOUT empty; graceful finish";

    cap_t cap; if (cap_start(stdout, &cap, "it4.out") != 0) { fail(TEST, "capture failed"); return; }

    const char* err = common_plugin_init(proc_identity_same, "it4", 2);
    if (err) { cap_stop(stdout, &cap, NULL); fail(TEST, err); return; }
    // no attach -> this plugin is last and should not print

    // send some inputs + <END>
    const char* inputs[] = {"alpha", "", "Beta"};
    for (size_t i = 0; i < sizeof(inputs)/sizeof(inputs[0]); ++i) plugin_place_work(inputs[i]);
    plugin_place_work("<END>");
    plugin_wait_finished();
    plugin_fini();

    char* out = NULL; cap_stop(stdout, &cap, &out);
    int ok = (!out || out[0] == '\0');
    if (ok) pass(TEST); else fail(TEST, "common printed to STDOUT unexpectedly");
    free(out);
}

// ===================== IT5 =====================

static void it5_backpressure_order_preserved(void){
    const char* TEST = "IT5: backpressure with slow consumer; order preserved";

    collect_reset();

    const char* err = common_plugin_init(proc_slow_same, "it5", 2 /*small queue*/);
    if (err) { fail(TEST, err); return; }
    plugin_attach(next_collect_no_print);

    const int N = 50;
    char buf[16];
    for (int i = 0; i < N; ++i) {
        snprintf(buf, sizeof(buf), "s%03d", i);
        plugin_place_work(buf);
    }
    plugin_place_work("<END>");
    plugin_wait_finished();
    plugin_fini();

    // Validate: exact count and exact order
    int ok = (g_collect_sz == N);
    for (int i = 0; ok && i < N; ++i) {
        snprintf(buf, sizeof(buf), "s%03d", i);
        if (strcmp(g_collect[i], buf) != 0) ok = 0;
    }
    if (ok) pass(TEST); else fail(TEST, "order/count mismatch");
    collect_reset();
}

// ===================== IT6 =====================

typedef struct { char prefix; int start; int count; } prod_args_t;

static void* producer_thread(void* arg){
    prod_args_t* a = (prod_args_t*)arg;
    char line[16];
    for (int i = 0; i < a->count; ++i) {
        snprintf(line, sizeof(line), "%c%03d", a->prefix, a->start + i);
        plugin_place_work(line);
    }
    return NULL;
}

static void it6_two_producers_parallel(void){
    const char* TEST = "IT6: two parallel producers -> all items delivered (set membership)";

    collect_reset();

    const char* err = common_plugin_init(proc_identity_same, "it6", 2 /* small queue to push backpressure */);
    if (err) { fail(TEST, err); return; }
    plugin_attach(next_collect_no_print);

    // Two producers pushing different label sets
    pthread_t t1, t2;
    prod_args_t a1 = { .prefix='A', .start=0,   .count=50 };
    prod_args_t a2 = { .prefix='B', .start=100, .count=50 };
    pthread_create(&t1, NULL, producer_thread, &a1);
    pthread_create(&t2, NULL, producer_thread, &a2);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    plugin_place_work("<END>");
    plugin_wait_finished();
    plugin_fini();

    // Validate: total count and that every expected item exists (order across producers is not enforced)
    int ok = (g_collect_sz == 100);
    char need[16];
    for (int i = 0; ok && i < 50; ++i) {
        snprintf(need, sizeof(need), "A%03d", i);
        if (!collect_contains(need)) ok = 0;
    }
    for (int i = 0; ok && i < 50; ++i) {
        snprintf(need, sizeof(need), "B%03d", 100 + i);
        if (!collect_contains(need)) ok = 0;
    }

    if (ok) pass(TEST); else fail(TEST, "missing items from parallel producers");
    collect_reset();
}

// ===================== IT7 =====================
// process_function returns NULL for one item -> pipeline continues; others forwarded
static void it7_process_failure_does_not_break_pipeline(void){
    const char* TEST = "IT7: single transform failure -> others still flow";

    collect_reset();

    // Capture stderr to verify "transform failed" log once
    cap_t cap_err; if (cap_start(stderr, &cap_err, "it7.err") != 0) { fail(TEST, "stderr capture failed"); return; }

    const char* err = common_plugin_init(proc_fail_on_pattern, "it7", 2);
    if (err) { cap_stop(stderr, &cap_err, NULL); fail(TEST, err); return; }
    plugin_attach(next_collect_no_print);

    const char* items[] = {"A", "FAILME", "B", "C"};
    for (size_t i=0;i<4;++i) plugin_place_work(items[i]);
    plugin_place_work("<END>");
    plugin_wait_finished();
    plugin_fini();

    char* serr = NULL; cap_stop(stderr, &cap_err, &serr);

    // Expect collected: A,B,C (3 items) in order; and "transform failed" once in stderr
    int ok_count = (g_collect_sz == 3)
                && strcmp(g_collect[0], "A")==0
                && strcmp(g_collect[1], "B")==0
                && strcmp(g_collect[2], "C")==0;
    int ok_log = (serr && strstr(serr, "transform failed") != NULL);

    if (ok_count && ok_log) pass(TEST);
    else {
        char why[256];
        snprintf(why,sizeof(why),"count/log mismatch (sz=%d, stderr='%s')", g_collect_sz, serr?serr:"");
        fail(TEST, why);
    }
    free(serr);
    collect_reset();
}

// ===================== IT8 =====================
// next_place_work returns error for one item -> error logged, others continue
static void it8_next_error_logged_but_pipeline_continues(void){
    const char* TEST = "IT8: next error logged; pipeline continues";

    collect_reset();

    cap_t cap_err; if (cap_start(stderr, &cap_err, "it8.err") != 0) { fail(TEST, "stderr capture failed"); return; }

    const char* err = common_plugin_init(proc_identity_same, "it8", 2);
    if (err) { cap_stop(stderr, &cap_err, NULL); fail(TEST, err); return; }
    plugin_attach(next_error_on_pattern);

    const char* items[] = {"X", "ERRME", "Y"};
    for (size_t i=0;i<3;++i) plugin_place_work(items[i]);
    plugin_place_work("<END>");
    plugin_wait_finished();
    plugin_fini();

    char* serr = NULL; cap_stop(stderr, &cap_err, &serr);

    // Expect collected: X and Y (ERRME not collected due to next error)
    int ok_collect = (g_collect_sz == 2)
                  && strcmp(g_collect[0], "X")==0
                  && strcmp(g_collect[1], "Y")==0;
    int ok_log = (serr && strstr(serr, "downstream error") != NULL);

    if (ok_collect && ok_log) pass(TEST);
    else fail(TEST, "missing collected items or error log");
    free(serr);
    collect_reset();
}

// ===================== IT9 =====================
// Single <END> should be forwarded exactly once through a two-stage stub chain
static void it9_end_forwarded_once_across_stub_chain(void){
    const char* TEST = "IT9: <END> forwarded exactly once through stub chain";

    g_end_count_stage1 = g_end_count_stage2 = 0;

    const char* err = common_plugin_init(proc_identity_same, "it9", 2);
    if (err) { fail(TEST, err); return; }
    plugin_attach(next_chain_stage1);

    plugin_place_work("data1");
    plugin_place_work("<END>");
    plugin_wait_finished();
    plugin_fini();

    // We only assert counts for <END>; (normal items may or may not be used)
    int ok = (g_end_count_stage1 == 1 && g_end_count_stage2 == 1);
    if (ok) pass(TEST);
    else {
        char why[64]; snprintf(why,sizeof(why),"counts: s1=%d s2=%d", g_end_count_stage1, g_end_count_stage2);
        fail(TEST, why);
    }
}

// ===================== IT10 =====================
// Multiple init→attach→work→wait→fini cycles without leaks/blocks
static void it10_multiple_cycles_robustness(void){
    const char* TEST = "IT10: multiple cycles robustness";

    const int CYCLES = 30;
    int ok = 1;

    for (int c = 0; ok && c < CYCLES; ++c) {
        collect_reset();

        const char* err = common_plugin_init(proc_identity_same, "it10", 2);
        if (err) { ok = 0; break; }
        plugin_attach(next_collect_no_print);

        // push few items unique to cycle
        char line[32];
        for (int i=0;i<5;++i){
            snprintf(line, sizeof(line), "c%02d_%d", c, i);
            plugin_place_work(line);
        }
        plugin_place_work("<END>");
        plugin_wait_finished();
        plugin_fini();

        // verify we saw exactly those 5
        if (g_collect_sz != 5) { ok = 0; break; }
        for (int i=0;i<5;++i){
            snprintf(line, sizeof(line), "c%02d_%d", c, i);
            if (strcmp(g_collect[i], line) != 0) { ok = 0; break; }
        }
    }

    collect_reset();
    if (ok) pass(TEST); else fail(TEST, "mismatch or cycle failure");
}


// ===================== main =====================
int main(void){
    it1_chain_end_only();
    it2_end_flows_not_printed();
    it3_logger_middle_intermediate_and_final();

    it4_no_printer_last_no_stdout();
    it5_backpressure_order_preserved();
    it6_two_producers_parallel();
    
    it7_process_failure_does_not_break_pipeline();
    it8_next_error_logged_but_pipeline_continues();
    it9_end_forwarded_once_across_stub_chain();
    it10_multiple_cycles_robustness();



    fprintf(stdout, "\n");
    if(g_tests_failed==0){
        fprintf(stdout, GREEN "All %d tests passed successfully.\n" NC, g_tests_run);
        return 0;
    }else{
        fprintf(stdout, RED "%d/%d tests failed.\n" NC, g_tests_failed, g_tests_run);
        return 1;
    }
}
