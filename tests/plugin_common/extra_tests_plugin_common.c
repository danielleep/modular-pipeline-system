// File: extra_tests_plugin_common.c
// Run from: /operating systems/tests/plugin_common
// Purpose: “Golden” Integration Tests 1–6 for plugin_common
//  1) <END> flows once across chain, never printed
//  2) plugin_wait_finished blocks then releases on <END>
//  3) plugin_fini closes gracefully (2nd call errors)
//  4) No printing in common when last plugin
//  5) Backpressure with slow consumer – order preserved
//  6) Two parallel producers – all items delivered
//
// Notes:
//  - Colored PASS/FAIL output
//  - Summary at end
//  - Uses only plugin_common + sync (no real plugins needed)

#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>

#include "../../plugins/plugin_common.h"

// ========== Colors & summary ==========
#define GREEN  "\033[0;32m"
#define RED    "\033[0;31m"
#define NC     "\033[0m"

static int g_tests_run = 0, g_tests_failed = 0;
static void pass(const char* name){ ++g_tests_run; fprintf(stdout, GREEN "[PASS]" NC " %s\n", name); }
static void fail(const char* name, const char* why){ ++g_tests_run; ++g_tests_failed; fprintf(stdout, RED "[FAIL]" NC " %s: %s\n", name, why); }

// ========== Capture helpers ==========
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

// ========== Small utils ==========
static void sleep_ms(long ms){
    struct timespec ts; ts.tv_sec = ms/1000; ts.tv_nsec = (ms%1000)*1000000L; nanosleep(&ts, NULL);
}
static int is_end_token(const char* s){ return (s && strcmp(s, "<END>")==0); }

// ========== Collection (no-print) ==========
static char** g_collect = NULL;
static int g_collect_sz = 0, g_collect_cap = 0;

static void collect_reset(void){
    for(int i=0;i<g_collect_sz;++i) free(g_collect[i]);
    free(g_collect); g_collect=NULL; g_collect_sz=0; g_collect_cap=0;
}
static void collect_push(const char* s){
    if(!s) s = "";
    if(g_collect_sz==g_collect_cap){
        int nc = g_collect_cap ? g_collect_cap*2 : 16;
        g_collect = (char**)realloc(g_collect, (size_t)nc * sizeof(*g_collect));
        g_collect_cap = nc;
    }
    g_collect[g_collect_sz++] = strdup(s);
}
static int collect_contains(const char* s){
    for(int i=0;i<g_collect_sz;++i) if(strcmp(g_collect[i], s)==0) return 1;
    return 0;
}

// ========== process stubs ==========
static const char* proc_identity_same(const char* in){ return in; }
static const char* proc_slow_same(const char* in){ sleep_ms(5); return in; }

// ========== next stubs ==========
static const char* next_collect_no_print(const char* s){
    if(!is_end_token(s)) collect_push(s);
    return NULL;
}

// For test #1: two-stage stub chain counting only <END>
static int g_end_count_stage1 = 0;
static int g_end_count_stage2 = 0;

static const char* next_chain_stage2(const char* s){
    if(is_end_token(s)) g_end_count_stage2++;
    return NULL;
}
static const char* next_chain_stage1(const char* s){
    if(is_end_token(s)) g_end_count_stage1++;
    return next_chain_stage2(s);
}

// ========== wait thread helper ==========
typedef struct { int done; const char* err; } wait_ctx_t;
static void* wait_thread(void* p){
    wait_ctx_t* w = (wait_ctx_t*)p;
    w->err = plugin_wait_finished();
    w->done = (w->err==NULL) ? 1 : -1;
    return NULL;
}

// ========== TEST 1: <END> flows once across chain, not printed ==========
static void t1_end_flows_once_and_not_printed(void){
    const char* TEST = "T1: <END> flows once across chain; never printed";

    cap_t cap; if(cap_start(stdout,&cap,"t1.out")!=0){ fail(TEST,"capture failed"); return; }

    g_end_count_stage1 = g_end_count_stage2 = 0;

    const char* err = common_plugin_init(proc_identity_same, "t1", 2);
    if(err){ cap_stop(stdout,&cap,NULL); fail(TEST, err); return; }
    plugin_attach(next_chain_stage1);

    plugin_place_work("<END>");
    plugin_wait_finished();
    plugin_fini();

    char* out=NULL; cap_stop(stdout,&cap,&out);

    int ok_stdout_empty = (!out || out[0]=='\0');
    int ok_counts = (g_end_count_stage1==1 && g_end_count_stage2==1);

    if(ok_stdout_empty && ok_counts) pass(TEST);
    else {
        char why[128]; snprintf(why,sizeof(why),"stdout:'%s' s1=%d s2=%d",
                                out?out:"", g_end_count_stage1, g_end_count_stage2);
        fail(TEST, why);
    }
    free(out);
}

// ========== TEST 2: wait_finished blocks then releases on <END> ==========
static void t2_wait_blocks_then_unblocks(void){
    const char* TEST = "T2: plugin_wait_finished blocks then releases on <END>";

    const char* err = common_plugin_init(proc_identity_same, "t2", 2);
    if(err){ fail(TEST, err); return; }

    wait_ctx_t wc = {.done=0,.err=NULL};
    pthread_t th;
    if(pthread_create(&th,NULL,wait_thread,&wc)!=0){ fail(TEST,"pthread_create failed"); (void)plugin_fini(); return; }

    sleep_ms(120);
    int still_blocked = (wc.done==0);
    plugin_place_work("<END>");
    pthread_join(th,NULL);

    const char* e2 = plugin_fini();
    int unblocked_ok = (wc.done==1 && wc.err==NULL && e2==NULL);

    if(still_blocked && unblocked_ok) pass(TEST);
    else fail(TEST, "wait did not block/unblock as expected");
}

// ========== TEST 3: plugin_fini graceful close (2nd call errors) ==========
static void t3_fini_graceful_and_idempotent(void){
    const char* TEST = "T3: plugin_fini graceful; second call returns error";

    const char* err = common_plugin_init(proc_identity_same, "t3", 2);
    if(err){ fail(TEST, err); return; }

    plugin_place_work("x");
    plugin_place_work("<END>");
    plugin_wait_finished();

    const char* e1 = plugin_fini();
    const char* e2 = plugin_fini();

    if(e1==NULL && e2!=NULL) pass(TEST);
    else fail(TEST, "unexpected fini behavior");
}

// ========== TEST 4: no printing in common when last plugin ==========
static void t4_last_plugin_no_stdout(void){
    const char* TEST = "T4: no printing in common when last plugin";

    cap_t cap; if(cap_start(stdout,&cap,"t4.out")!=0){ fail(TEST,"capture failed"); return; }

    const char* err = common_plugin_init(proc_identity_same, "t4", 2);
    if(err){ cap_stop(stdout,&cap,NULL); fail(TEST, err); return; }
    // no attach -> last in chain, common must not print

    plugin_place_work("a");
    plugin_place_work("");
    plugin_place_work("b");
    plugin_place_work("<END>");
    plugin_wait_finished();
    plugin_fini();

    char* out=NULL; cap_stop(stdout,&cap,&out);
    int ok = (!out || out[0]=='\0');
    if(ok) pass(TEST); else fail(TEST, "common printed unexpectedly");
    free(out);
}

// ========== TEST 5: backpressure with slow consumer; order preserved ==========
static void t5_backpressure_order_preserved(void){
    const char* TEST = "T5: backpressure (slow consumer); order preserved";

    collect_reset();

    const char* err = common_plugin_init(proc_slow_same, "t5", 2 /*small queue*/);
    if(err){ fail(TEST, err); return; }
    plugin_attach(next_collect_no_print);

    const int N = 50;
    char buf[16];
    for(int i=0;i<N;++i){ snprintf(buf,sizeof(buf),"s%03d",i); plugin_place_work(buf); }
    plugin_place_work("<END>");
    plugin_wait_finished();
    plugin_fini();

    int ok = (g_collect_sz==N);
    for(int i=0; ok && i<N; ++i){ snprintf(buf,sizeof(buf),"s%03d",i); if(strcmp(g_collect[i],buf)!=0) ok=0; }

    if(ok) pass(TEST); else fail(TEST, "order/count mismatch");
    collect_reset();
}

// ========== TEST 6: two parallel producers; all items delivered ==========
typedef struct { char prefix; int start; int count; } prod_args_t;
static void* producer_thread(void* arg){
    prod_args_t* a = (prod_args_t*)arg;
    char line[16];
    for(int i=0;i<a->count;++i){ snprintf(line,sizeof(line),"%c%03d",a->prefix,a->start+i); plugin_place_work(line); }
    return NULL;
}
static void t6_two_producers_parallel(void){
    const char* TEST = "T6: two parallel producers -> all items delivered";

    collect_reset();

    const char* err = common_plugin_init(proc_identity_same, "t6", 2);
    if(err){ fail(TEST, err); return; }
    plugin_attach(next_collect_no_print);

    pthread_t t1, t2;
    prod_args_t a1 = {.prefix='A', .start=0,   .count=50};
    prod_args_t a2 = {.prefix='B', .start=100, .count=50};
    pthread_create(&t1,NULL,producer_thread,&a1);
    pthread_create(&t2,NULL,producer_thread,&a2);
    pthread_join(t1,NULL);
    pthread_join(t2,NULL);

    plugin_place_work("<END>");
    plugin_wait_finished();
    plugin_fini();

    int ok = (g_collect_sz==100);
    char need[16];
    for(int i=0; ok && i<50; ++i){ snprintf(need,sizeof(need),"A%03d",i); if(!collect_contains(need)) ok=0; }
    for(int i=0; ok && i<50; ++i){ snprintf(need,sizeof(need),"B%03d",100+i); if(!collect_contains(need)) ok=0; }

    if(ok) pass(TEST); else fail(TEST, "missing items from parallel producers");
    collect_reset();
}

// ========== main ==========
int main(void){
    t1_end_flows_once_and_not_printed();
    t2_wait_blocks_then_unblocks();
    t3_fini_graceful_and_idempotent();
    t4_last_plugin_no_stdout();
    t5_backpressure_order_preserved();
    t6_two_producers_parallel();

    fprintf(stdout, "\n");
    if(g_tests_failed==0){
        fprintf(stdout, GREEN "All %d tests passed successfully.\n" NC, g_tests_run);
        return 0;
    }else{
        fprintf(stdout, RED "%d/%d tests failed.\n" NC, g_tests_failed, g_tests_run);
        return 1;
    }
}
