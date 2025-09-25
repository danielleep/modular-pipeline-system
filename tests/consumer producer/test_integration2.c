// tests/consumer producer/test_integration2.c
// Integration tests (part 1: cases 1-5) for consumer_producer queue.
// - Colored PASS/FAIL output
// - Thread-safe result aggregation
// - Start barrier for synchronized launch
// - Pure C (C11), no non-standard extensions
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>



#include "consumer_producer.h"   // provided via -I plugins/sync

// ---------- Colored output helpers ----------
#define CRED   "\x1b[31m"
#define CGREEN "\x1b[32m"
#define CYELL  "\x1b[33m"
#define CRESET "\x1b[0m"

#define PRINT_PASS(name)  printf(CGREEN "[PASS]" CRESET " %s\n", name)
#define PRINT_FAIL(name)  printf(CRED   "[FAIL]" CRESET " %s\n", name)

// Global counters for summary
static int g_total_tests  = 0;
static int g_failed_tests = 0;

// Replace previous RUN_TEST with this version:
#undef RUN_TEST
#define RUN_TEST(fn) do {                       \
    g_total_tests++;                            \
    int _rc = (fn)();                           \
    if (_rc == 0) {                             \
        PRINT_PASS(#fn);                        \
    } else {                                     \
        PRINT_FAIL(#fn);                        \
        g_failed_tests++;                       \
    }                                           \
} while (0)


// ---------- Small utilities ----------

// Replace tiny_sleep_us to use nanosleep (portable with C11 + POSIX)
static void tiny_sleep_us(int us_min, int us_max) {
    int us;
    if (us_max <= us_min) {
        if (us_min <= 0) return;
        us = us_min;
    } else {
        int span = us_max - us_min + 1;
        us = us_min + (rand() % span);
    }

    struct timespec ts;
    ts.tv_sec  = us / 1000000;
    ts.tv_nsec = (long)(us % 1000000) * 1000L;

    // Handle EINTR
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { /* retry */ }
}


static char* make_item_number(int number) {
    // Allocate a decimal string for the token id; queue takes ownership on put
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d", number);
    char* s = (char*)malloc((size_t)n + 1);
    if (!s) return NULL;
    memcpy(s, buf, (size_t)n + 1);
    return s;
}

static int parse_item_number(const char* s) {
    // Convert item string back to integer token id
    return (int)strtol(s, NULL, 10);
}

// ---------- Simple start barrier (portable replacement for pthread_barrier_t) ----------
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  cv;
    int count;
    int total;
} start_barrier_t;

static void start_barrier_init(start_barrier_t* b, int total) {
    pthread_mutex_init(&b->m, NULL);
    pthread_cond_init(&b->cv, NULL);
    b->count = 0;
    b->total = total;
}
static void start_barrier_wait(start_barrier_t* b) {
    pthread_mutex_lock(&b->m);
    b->count++;
    if (b->count >= b->total) {
        pthread_cond_broadcast(&b->cv);
    } else {
        while (b->count < b->total) {
            pthread_cond_wait(&b->cv, &b->m);
        }
    }
    pthread_mutex_unlock(&b->m);
}
static void start_barrier_destroy(start_barrier_t* b) {
    pthread_mutex_destroy(&b->m);
    pthread_cond_destroy(&b->cv);
}

// ---------- Result aggregator ----------
typedef struct {
    pthread_mutex_t m;
    int received;         // how many unique items recorded
    int duplicates;       // items seen more than once
    int out_of_range;     // unexpected token id
    int errors;           // put/get operational errors
    int N;                // expected unique items (size of seen[])
    unsigned char* seen;  // bitmap (0/1) for token ids [0..N-1]
} results_t;

static int results_init(results_t* r, int N) {
    if (pthread_mutex_init(&r->m, NULL) != 0) return -1;
    r->received = r->duplicates = r->out_of_range = r->errors = 0;
    r->N = N;
    r->seen = (unsigned char*)calloc((size_t)N, 1);
    return r->seen ? 0 : -1;
}
static void results_destroy(results_t* r) {
    free(r->seen);
    r->seen = NULL;
    pthread_mutex_destroy(&r->m);
}
static void results_record(results_t* r, int token) {
    pthread_mutex_lock(&r->m);
    if (token < 0 || token >= r->N) {
        r->out_of_range++;
    } else if (r->seen[token] == 0) {
        r->seen[token] = 1;
        r->received++;
    } else {
        r->duplicates++;
    }
    pthread_mutex_unlock(&r->m);
}
static void results_add_error(results_t* r) {
    pthread_mutex_lock(&r->m);
    r->errors++;
    pthread_mutex_unlock(&r->m);
}
static int results_ok(const results_t* r) {
    return (r->received == r->N) && (r->duplicates == 0) && (r->out_of_range == 0) && (r->errors == 0);
}
static void results_print(const results_t* r, const char* label) {
    printf(CYELL "[INFO]" CRESET " %s: received=%d/%d, duplicates=%d, out_of_range=%d, errors=%d\n",
           label, r->received, r->N, r->duplicates, r->out_of_range, r->errors);
}

// ---------- Thread args ----------
typedef struct {
    consumer_producer_t* q;
    start_barrier_t* gate;
    int producer_id;
    int items_per_producer;
    results_t* res;     // for error counting (puts)
    int with_delays;    // stress mode
} producer_args_t;

typedef struct {
    consumer_producer_t* q;
    start_barrier_t* gate;
    results_t* res;
    int with_delays;    // stress mode
} consumer_args_t;

// ---------- Producer thread ----------
static void* producer_thread(void* arg) {
    producer_args_t* a = (producer_args_t*)arg;
    start_barrier_wait(a->gate);

    for (int i = 0; i < a->items_per_producer; ++i) {
        int token = a->producer_id * a->items_per_producer + i;
        char* item = make_item_number(token);
        if (!item) { results_add_error(a->res); continue; }

        const char* err = consumer_producer_put(a->q, item);
        if (err != NULL) {
            // put failed; caller retains ownership -> free here
            free(item);
            results_add_error(a->res);
        }
        if (a->with_delays) tiny_sleep_us(0, 200);
    }
    return NULL;
}

// ---------- Consumer thread ----------
static void* consumer_thread(void* arg) {
    consumer_args_t* a = (consumer_args_t*)arg;
    start_barrier_wait(a->gate);

    for (;;) {
        char* s = consumer_producer_get(a->q);
        if (s == NULL) break; // finished & empty
        int token = parse_item_number(s);
        results_record(a->res, token);
        free(s);
        if (a->with_delays) tiny_sleep_us(0, 200);
    }
    return NULL;
}

// ---------- Helpers: create/join thread arrays ----------
static int create_thread_array(pthread_t* arr, int n, void* (*fn)(void*), void** args_array) {
    for (int i = 0; i < n; ++i) {
        if (pthread_create(&arr[i], NULL, fn, args_array ? args_array[i] : NULL) != 0) {
            return -1;
        }
    }
    return 0;
}
static void join_thread_array(pthread_t* arr, int n) {
    for (int i = 0; i < n; ++i) {
        (void)pthread_join(arr[i], NULL);
    }
}

// ---------- helpers for tests 8 & 10 ----------
typedef struct {
    consumer_producer_t* q;
    int token;                 // item id to push
    const char** err_out;      // store put() return
    volatile int* done_flag;   // set to 1 when put returns
} single_put_args_t;

static void* single_put_thread(void* arg) {
    single_put_args_t* a = (single_put_args_t*)arg;
    char* item = make_item_number(a->token);
    const char* e = consumer_producer_put(a->q, item);
    if (a->err_out) *a->err_out = e;     // NULL on success
    if (a->done_flag) *(a->done_flag) = 1;
    return NULL;
}

typedef struct {
    consumer_producer_t* q;
    start_barrier_t* gate;
    results_t* res;
    int with_delays;
    volatile int* finished_flag_out;     // set to 1 right before thread returns
} consumer_mark_args_t;

static void* consumer_thread_mark(void* arg) {
    consumer_mark_args_t* a = (consumer_mark_args_t*)arg;
    start_barrier_wait(a->gate);
    for (;;) {
        char* s = consumer_producer_get(a->q);
        if (!s) break;
        int token = parse_item_number(s);
        results_record(a->res, token);
        free(s);
        if (a->with_delays) tiny_sleep_us(100, 300);
    }
    if (a->finished_flag_out) *(a->finished_flag_out) = 1;
    return NULL;
}

typedef struct {
    consumer_producer_t* q;
    volatile int* done_flag;            // set to 1 when wait_finished() returns
} waiter_args_t;

static void* waiter_thread(void* arg) {
    waiter_args_t* a = (waiter_args_t*)arg;
    (void)consumer_producer_wait_finished(a->q);
    if (a->done_flag) *(a->done_flag) = 1;
    return NULL;
}

// ---- helpers for test #11 (detect NULL before finished is announced) ----
typedef struct {
    consumer_producer_t* q;
    start_barrier_t* gate;
    results_t* res;
    volatile int* finished_announced;   // set by main right before signal_finished()
    volatile int* null_before_finish;   // set to 1 if a consumer gets NULL while finished_announced==0
} consumer_pre_finish_args_t;

static void* consumer_thread_pre_finish(void* arg) {
    consumer_pre_finish_args_t* a = (consumer_pre_finish_args_t*)arg;
    start_barrier_wait(a->gate);
    for (;;) {
        char* s = consumer_producer_get(a->q);
        if (s == NULL) {
            if (*(a->finished_announced) == 0) {
                *(a->null_before_finish) = 1; // observed NULL before finished() was called
            }
            break;
        }
        int token = parse_item_number(s);
        results_record(a->res, token);
        free(s);
    }
    return NULL;
}

// ---- helpers for test #12 (count how many consumers exited with NULL) ----
typedef struct {
    consumer_producer_t* q;
    start_barrier_t* gate;
    volatile int* exited_counter; // increment when thread returns (i.e., got NULL)
} consumer_exit_counter_args_t;

static void* consumer_exit_counter_thread(void* arg) {
    consumer_exit_counter_args_t* a = (consumer_exit_counter_args_t*)arg;
    start_barrier_wait(a->gate);
    char* s = consumer_producer_get(a->q);
    if (s) { free(s); } // should not happen in this test, but safe
    __sync_fetch_and_add(a->exited_counter, 1);
    return NULL;
}



// ======================================================================
// 1) test_parallel_put_get   (m producers, k consumers)
// ======================================================================
static int test_parallel_put_get(void) {
    srand((unsigned int)time(NULL));
    const int capacity = 8;
    const int M = 3; // producers
    const int K = 4; // consumers
    const int ITEMS_PER_PRODUCER = 1500;
    const int N = M * ITEMS_PER_PRODUCER;

    consumer_producer_t q;
    const char* err = consumer_producer_init(&q, capacity);
    if (err) { fprintf(stderr, "init error: %s\n", err); return 1; }

    results_t res;
    if (results_init(&res, N) != 0) { consumer_producer_destroy(&q); return 1; }

    start_barrier_t gate;
    start_barrier_init(&gate, M + K);

    pthread_t *producers = (pthread_t*)malloc(sizeof(pthread_t)*M);
    pthread_t *consumers = (pthread_t*)malloc(sizeof(pthread_t)*K);
    producer_args_t *pargs = (producer_args_t*)malloc(sizeof(producer_args_t)*M);
    consumer_args_t *cargs = (consumer_args_t*)malloc(sizeof(consumer_args_t)*K);
    void **pargs_ptrs = (void**)malloc(sizeof(void*)*M);
    void **cargs_ptrs = (void**)malloc(sizeof(void*)*K);
    if (!producers || !consumers || !pargs || !cargs || !pargs_ptrs || !cargs_ptrs) return 1;

    for (int p = 0; p < M; ++p) {
        pargs[p] = (producer_args_t){ .q=&q, .gate=&gate, .producer_id=p,
                                      .items_per_producer=ITEMS_PER_PRODUCER,
                                      .res=&res, .with_delays=0 };
        pargs_ptrs[p] = &pargs[p];
    }
    for (int c = 0; c < K; ++c) {
        cargs[c] = (consumer_args_t){ .q=&q, .gate=&gate, .res=&res, .with_delays=0 };
        cargs_ptrs[c] = &cargs[c];
    }

    if (create_thread_array(producers, M, producer_thread, pargs_ptrs) != 0) return 1;
    if (create_thread_array(consumers, K, consumer_thread, cargs_ptrs) != 0) return 1;

    join_thread_array(producers, M);
    consumer_producer_signal_finished(&q);
    join_thread_array(consumers, K);

    if (consumer_producer_wait_finished(&q) != 0) return 1;

    results_print(&res, "parallel_put_get");
    int ok = results_ok(&res);

    free(producers); free(consumers); free(pargs); free(cargs); free(pargs_ptrs); free(cargs_ptrs);
    results_destroy(&res);
    start_barrier_destroy(&gate);
    consumer_producer_destroy(&q);
    return ok ? 0 : 1;
}

// ======================================================================
// 2) test_stress
// ======================================================================
static int test_stress(void) {
    srand((unsigned int)time(NULL) ^ 0xA5A5A5A5u);
    const int capacity = 32;
    const int M = 6, K = 6, ITEMS_PER_PRODUCER = 3000;
    const int N = M * ITEMS_PER_PRODUCER;

    consumer_producer_t q;
    const char* err = consumer_producer_init(&q, capacity);
    if (err) { fprintf(stderr, "init error: %s\n", err); return 1; }

    results_t res;
    if (results_init(&res, N) != 0) { consumer_producer_destroy(&q); return 1; }

    start_barrier_t gate;
    start_barrier_init(&gate, M + K);

    pthread_t *producers = (pthread_t*)malloc(sizeof(pthread_t)*M);
    pthread_t *consumers = (pthread_t*)malloc(sizeof(pthread_t)*K);
    producer_args_t *pargs = (producer_args_t*)malloc(sizeof(producer_args_t)*M);
    consumer_args_t *cargs = (consumer_args_t*)malloc(sizeof(consumer_args_t)*K);
    void **pargs_ptrs = (void**)malloc(sizeof(void*)*M);
    void **cargs_ptrs = (void**)malloc(sizeof(void*)*K);
    if (!producers || !consumers || !pargs || !cargs || !pargs_ptrs || !cargs_ptrs) return 1;

    for (int p = 0; p < M; ++p) {
        pargs[p] = (producer_args_t){ .q=&q, .gate=&gate, .producer_id=p,
                                      .items_per_producer=ITEMS_PER_PRODUCER,
                                      .res=&res, .with_delays=1 };
        pargs_ptrs[p] = &pargs[p];
    }
    for (int c = 0; c < K; ++c) {
        cargs[c] = (consumer_args_t){ .q=&q, .gate=&gate, .res=&res, .with_delays=1 };
        cargs_ptrs[c] = &cargs[c];
    }

    if (create_thread_array(producers, M, producer_thread, pargs_ptrs) != 0) return 1;
    if (create_thread_array(consumers, K, consumer_thread, cargs_ptrs) != 0) return 1;

    join_thread_array(producers, M);
    consumer_producer_signal_finished(&q);
    join_thread_array(consumers, K);

    if (consumer_producer_wait_finished(&q) != 0) return 1;

    results_print(&res, "stress");
    int ok = results_ok(&res);

    free(producers); free(consumers); free(pargs); free(cargs); free(pargs_ptrs); free(cargs_ptrs);
    results_destroy(&res);
    start_barrier_destroy(&gate);
    consumer_producer_destroy(&q);
    return ok ? 0 : 1;
}

// ======================================================================
// 3) test_multiple_producers   (many producers, single consumer)
// ======================================================================
static int test_multiple_producers(void) {
    const int capacity = 8, M = 4, K = 1, ITEMS = 1200, N = M * ITEMS;

    consumer_producer_t q;
    const char* err = consumer_producer_init(&q, capacity);
    if (err) { fprintf(stderr, "init error: %s\n", err); return 1; }

    results_t res; if (results_init(&res, N) != 0) { consumer_producer_destroy(&q); return 1; }
    start_barrier_t gate; start_barrier_init(&gate, M + K);

    pthread_t *producers = (pthread_t*)malloc(sizeof(pthread_t)*M);
    pthread_t *consumers = (pthread_t*)malloc(sizeof(pthread_t)*K);
    producer_args_t *pargs = (producer_args_t*)malloc(sizeof(producer_args_t)*M);
    consumer_args_t *cargs = (consumer_args_t*)malloc(sizeof(consumer_args_t)*K);
    void **pargs_ptrs = (void**)malloc(sizeof(void*)*M);
    void **cargs_ptrs = (void**)malloc(sizeof(void*)*K);
    if (!producers || !consumers || !pargs || !cargs || !pargs_ptrs || !cargs_ptrs) return 1;

    for (int p=0; p<M; ++p) {
        pargs[p]=(producer_args_t){ .q=&q, .gate=&gate, .producer_id=p,
                                    .items_per_producer=ITEMS, .res=&res, .with_delays=0 };
        pargs_ptrs[p]=&pargs[p];
    }
    for (int c=0; c<K; ++c) {
        cargs[c]=(consumer_args_t){ .q=&q, .gate=&gate, .res=&res, .with_delays=0 };
        cargs_ptrs[c]=&cargs[c];
    }

    if (create_thread_array(producers, M, producer_thread, pargs_ptrs)!=0) return 1;
    if (create_thread_array(consumers, K, consumer_thread, cargs_ptrs)!=0) return 1;

    join_thread_array(producers, M);
    consumer_producer_signal_finished(&q);
    join_thread_array(consumers, K);
    if (consumer_producer_wait_finished(&q)!=0) return 1;

    int ok = results_ok(&res);

    free(producers); free(consumers); free(pargs); free(cargs); free(pargs_ptrs); free(cargs_ptrs);
    results_destroy(&res); start_barrier_destroy(&gate); consumer_producer_destroy(&q);
    return ok?0:1;
}

// ======================================================================
// 4) test_multiple_consumers   (single producer, many consumers)
// ======================================================================
static int test_multiple_consumers(void) {
    const int capacity = 8, M = 1, K = 5, ITEMS = 4000, N = M * ITEMS;

    consumer_producer_t q; const char* err = consumer_producer_init(&q, capacity);
    if (err) { fprintf(stderr, "init error: %s\n", err); return 1; }

    results_t res; if (results_init(&res, N)!=0) { consumer_producer_destroy(&q); return 1; }
    start_barrier_t gate; start_barrier_init(&gate, M + K);

    pthread_t *producers = (pthread_t*)malloc(sizeof(pthread_t)*M);
    pthread_t *consumers = (pthread_t*)malloc(sizeof(pthread_t)*K);
    producer_args_t *pargs = (producer_args_t*)malloc(sizeof(producer_args_t)*M);
    consumer_args_t *cargs = (consumer_args_t*)malloc(sizeof(consumer_args_t)*K);
    void **pargs_ptrs = (void**)malloc(sizeof(void*)*M);
    void **cargs_ptrs = (void**)malloc(sizeof(void*)*K);
    if (!producers || !consumers || !pargs || !cargs || !pargs_ptrs || !cargs_ptrs) return 1;

    pargs[0]=(producer_args_t){ .q=&q, .gate=&gate, .producer_id=0,
                                .items_per_producer=ITEMS, .res=&res, .with_delays=0 };
    pargs_ptrs[0]=&pargs[0];
    for (int c=0; c<K; ++c){ cargs[c]=(consumer_args_t){ .q=&q, .gate=&gate, .res=&res, .with_delays=0 }; cargs_ptrs[c]=&cargs[c]; }

    if (create_thread_array(producers, M, producer_thread, pargs_ptrs)!=0) return 1;
    if (create_thread_array(consumers, K, consumer_thread, cargs_ptrs)!=0) return 1;

    join_thread_array(producers, M);
    consumer_producer_signal_finished(&q);
    join_thread_array(consumers, K);
    if (consumer_producer_wait_finished(&q)!=0) return 1;

    int ok = results_ok(&res);

    free(producers); free(consumers); free(pargs); free(cargs); free(pargs_ptrs); free(cargs_ptrs);
    results_destroy(&res); start_barrier_destroy(&gate); consumer_producer_destroy(&q);
    return ok?0:1;
}

// ======================================================================
// 5) test_capacity_one_high_contention   (capacity=1, contention)
// ======================================================================
static int test_capacity_one_high_contention(void) {
    const int capacity = 1, M = 2, K = 2, ITEMS = 3000, N = M * ITEMS;

    consumer_producer_t q; const char* err = consumer_producer_init(&q, capacity);
    if (err) { fprintf(stderr,"init error: %s\n",err); return 1; }

    results_t res; if (results_init(&res, N)!=0) { consumer_producer_destroy(&q); return 1; }
    start_barrier_t gate; start_barrier_init(&gate, M + K);

    pthread_t *producers = (pthread_t*)malloc(sizeof(pthread_t)*M);
    pthread_t *consumers = (pthread_t*)malloc(sizeof(pthread_t)*K);
    producer_args_t *pargs = (producer_args_t*)malloc(sizeof(producer_args_t)*M);
    consumer_args_t *cargs = (consumer_args_t*)malloc(sizeof(consumer_args_t)*K);
    void **pargs_ptrs = (void**)malloc(sizeof(void*)*M);
    void **cargs_ptrs = (void**)malloc(sizeof(void*)*K);
    if (!producers || !consumers || !pargs || !cargs || !pargs_ptrs || !cargs_ptrs) return 1;

    for(int p=0;p<M;++p){
        pargs[p]=(producer_args_t){ .q=&q, .gate=&gate, .producer_id=p,
                                    .items_per_producer=ITEMS, .res=&res, .with_delays=1 };
        pargs_ptrs[p]=&pargs[p];
    }
    for(int c=0;c<K;++c){
        cargs[c]=(consumer_args_t){ .q=&q, .gate=&gate, .res=&res, .with_delays=1 };
        cargs_ptrs[c]=&cargs[c];
    }

    if (create_thread_array(producers, M, producer_thread, pargs_ptrs)!=0) return 1;
    if (create_thread_array(consumers, K, consumer_thread, cargs_ptrs)!=0) return 1;

    join_thread_array(producers, M);
    consumer_producer_signal_finished(&q);
    join_thread_array(consumers, K);
    if (consumer_producer_wait_finished(&q)!=0) return 1;

    int ok = results_ok(&res);

    free(producers); free(consumers); free(pargs); free(cargs); free(pargs_ptrs); free(cargs_ptrs);
    results_destroy(&res); start_barrier_destroy(&gate); consumer_producer_destroy(&q);
    return ok?0:1;
}

// ======================================================================
// 6) test_wraparound_indices
// ======================================================================
static int test_wraparound_indices(void) {
    const int capacity = 3, M = 1, K = 1, ITEMS = 5000, N = M * ITEMS;

    consumer_producer_t q; const char* err = consumer_producer_init(&q, capacity);
    if (err) { fprintf(stderr,"init error: %s\n", err); return 1; }

    results_t res; if (results_init(&res, N)!=0) { consumer_producer_destroy(&q); return 1; }
    start_barrier_t gate; start_barrier_init(&gate, M + K);

    pthread_t pt, ct;
    producer_args_t parg = { .q=&q, .gate=&gate, .producer_id=0, .items_per_producer=ITEMS, .res=&res, .with_delays=0 };
    consumer_args_t carg = { .q=&q, .gate=&gate, .res=&res, .with_delays=0 };

    if (pthread_create(&pt, NULL, producer_thread, &parg)!=0) return 1;
    if (pthread_create(&ct, NULL, consumer_thread, &carg)!=0) return 1;

    pthread_join(pt, NULL);
    consumer_producer_signal_finished(&q);
    pthread_join(ct, NULL);

    if (consumer_producer_wait_finished(&q)!=0) return 1;

    results_print(&res, "wraparound_indices");
    int ok = results_ok(&res);

    results_destroy(&res); start_barrier_destroy(&gate); consumer_producer_destroy(&q);
    return ok?0:1;
}

// ======================================================================
// 7) test_finish_without_items
// ======================================================================
static int test_finish_without_items(void) {
    consumer_producer_t q; const char* err = consumer_producer_init(&q, 4);
    if (err) { fprintf(stderr,"init error: %s\n", err); return 1; }

    const int K = 3;
    start_barrier_t gate; start_barrier_init(&gate, K);
    results_t res; (void)results_init(&res, 0); // no items expected

    pthread_t consumers[K];
    consumer_args_t cargs[K]; void* cptrs[K];
    for (int c=0;c<K;++c){ cargs[c]=(consumer_args_t){.q=&q,.gate=&gate,.res=&res,.with_delays=0}; cptrs[c]=&cargs[c]; }
    if (create_thread_array(consumers,K,consumer_thread,cptrs)!=0) return 1;

    consumer_producer_signal_finished(&q);
    join_thread_array(consumers,K);

    int ok = (consumer_producer_wait_finished(&q)==0);

    start_barrier_destroy(&gate); results_destroy(&res); consumer_producer_destroy(&q);
    return ok?0:1;
}

// ======================================================================
// 8) test_producer_blocked_then_finished
// Capacity=1: second put blocks; signal_finished arrives while blocked.
// That put must still complete; new puts after finished must be rejected.
// ======================================================================
static int test_producer_blocked_then_finished(void) {
    consumer_producer_t q; const char* err = consumer_producer_init(&q, 1);
    if (err) { fprintf(stderr,"init error: %s\n", err); return 1; }

    // Fill the queue (token 0) so next put will block
    char* first = make_item_number(0); if (!first) { consumer_producer_destroy(&q); return 1; }
    if (consumer_producer_put(&q, first) != NULL) { free(first); consumer_producer_destroy(&q); return 1; }

    // Launch a thread that attempts to put token 1 (will block until space)
    pthread_t pth;
    volatile int put2_done = 0;
    const char* put2_err = (const char*)0xdeadbeef; // sentinel
    single_put_args_t pa = { .q=&q, .token=1, .err_out=&put2_err, .done_flag=&put2_done };
    if (pthread_create(&pth, NULL, single_put_thread, &pa)!=0) { consumer_producer_destroy(&q); return 1; }

    // Give it a moment to block
    tiny_sleep_us(10000, 20000);
    if (put2_done) { fprintf(stderr,"unexpected: second put did not block\n"); return 1; }

    // Now signal finished WHILE the producer is blocked
    consumer_producer_signal_finished(&q);

    // Consume one item to free space (should be token 0)
    char* got = consumer_producer_get(&q);
    if (!got) { fprintf(stderr,"get returned NULL unexpectedly\n"); return 1; }
    int t0 = parse_item_number(got); free(got);
    if (t0 != 0) { fprintf(stderr,"expected token 0 first, got %d\n", t0); return 1; }

    // The blocked put (token 1) must now complete
    pthread_join(pth, NULL);
    if (put2_err != NULL) { fprintf(stderr,"second put failed after finished\n"); return 1; }

    // Any NEW put after finished must be rejected
    char* third = make_item_number(2);
    const char* perr = consumer_producer_put(&q, third);
    if (perr == NULL) { fprintf(stderr,"new put after finished unexpectedly succeeded\n"); return 1; }
    free(third); // caller retains ownership on failure

    // Drain remaining items (should get token 1) and finish
    char* s = consumer_producer_get(&q);
    if (!s) { fprintf(stderr,"expected one more item (token 1)\n"); return 1; }
    int t1 = parse_item_number(s); free(s);
    if (t1 != 1) { fprintf(stderr,"expected token 1, got %d\n", t1); return 1; }

    // Now the queue is empty and finished: next get must return NULL
    if (consumer_producer_get(&q) != NULL) { fprintf(stderr,"expected NULL after drain+finished\n"); return 1; }

    int ok = (consumer_producer_wait_finished(&q)==0);
    consumer_producer_destroy(&q);
    return ok?0:1;
}

// ======================================================================
// 9) test_double_signal_finished_idempotent
// ======================================================================
static int test_double_signal_finished_idempotent(void) {
    const int capacity = 4, M = 1, K = 1, ITEMS = 100, N = M*ITEMS;

    consumer_producer_t q; const char* err = consumer_producer_init(&q, capacity);
    if (err) { fprintf(stderr,"init error: %s\n", err); return 1; }

    results_t res; if (results_init(&res, N)!=0) { consumer_producer_destroy(&q); return 1; }
    start_barrier_t gate; start_barrier_init(&gate, M + K);

    pthread_t pt, ct;
    producer_args_t parg = { .q=&q, .gate=&gate, .producer_id=0, .items_per_producer=ITEMS, .res=&res, .with_delays=1 };
    consumer_args_t carg = { .q=&q, .gate=&gate, .res=&res, .with_delays=1 };

    if (pthread_create(&pt, NULL, producer_thread, &parg)!=0) return 1;
    if (pthread_create(&ct, NULL, consumer_thread, &carg)!=0) return 1;

    pthread_join(pt, NULL);
    // Call signal_finished twice; should be idempotent
    consumer_producer_signal_finished(&q);
    consumer_producer_signal_finished(&q);
    pthread_join(ct, NULL);

    if (consumer_producer_wait_finished(&q)!=0) return 1;

    int ok = results_ok(&res);
    results_destroy(&res); start_barrier_destroy(&gate); consumer_producer_destroy(&q);
    return ok?0:1;
}

// ======================================================================
// 10) test_wait_finished_blocks_until_empty
// wait_finished must return only after (finished==1 AND count==0).
// ======================================================================
static int test_wait_finished_blocks_until_empty(void) {
    const int capacity = 4, M=1, K=1, ITEMS=1000, N=M*ITEMS;

    consumer_producer_t q; const char* err = consumer_producer_init(&q, capacity);
    if (err) { fprintf(stderr,"init error: %s\n", err); return 1; }

    results_t res; if (results_init(&res, N)!=0) { consumer_producer_destroy(&q); return 1; }
    start_barrier_t gate; start_barrier_init(&gate, M + K);

    // Start slow consumer (with delays) and regular producer
    volatile int consumer_done = 0;
    pthread_t pt, ct, wt;

    producer_args_t parg = { .q=&q, .gate=&gate, .producer_id=0, .items_per_producer=ITEMS, .res=&res, .with_delays=0 };
    consumer_mark_args_t carg = { .q=&q, .gate=&gate, .res=&res, .with_delays=1, .finished_flag_out=&consumer_done };

    if (pthread_create(&pt, NULL, producer_thread, &parg)!=0) return 1;
    if (pthread_create(&ct, NULL, consumer_thread_mark, &carg)!=0) return 1;

    // Start a waiter thread that should not finish until the queue is fully drained
    volatile int wait_done = 0;
    waiter_args_t warg = { .q=&q, .done_flag=&wait_done };
    if (pthread_create(&wt, NULL, waiter_thread, &warg)!=0) return 1;

    // Let them run a bit, then signal finished
    pthread_join(pt, NULL);
    consumer_producer_signal_finished(&q);

    // Join consumer, then waiter must be able to complete
    pthread_join(ct, NULL);
    pthread_join(wt, NULL);

    int ok = results_ok(&res) && wait_done && consumer_done;
    results_destroy(&res); start_barrier_destroy(&gate); consumer_producer_destroy(&q);
    return ok?0:1;
}

// ======================================================================
// 11) test_no_null_before_finished
// Consumers must NOT receive NULL before signal_finished() is called.
// ======================================================================
static int test_no_null_before_finished(void) {
    const int capacity = 4;
    const int M = 1;               // one producer
    const int K = 2;               // two consumers
    const int ITEMS = 200;         // few items; consumers will drain and block
    const int N = M * ITEMS;

    consumer_producer_t q; const char* err = consumer_producer_init(&q, capacity);
    if (err) { fprintf(stderr,"init error: %s\n", err); return 1; }

    results_t res; if (results_init(&res, N) != 0) { consumer_producer_destroy(&q); return 1; }

    start_barrier_t gate; start_barrier_init(&gate, M + K); // sync prod+cons start

    // Start producer (does NOT signal finished)
    pthread_t pt;
    producer_args_t parg = { .q=&q, .gate=&gate, .producer_id=0,
                             .items_per_producer=ITEMS, .res=&res, .with_delays=0 };
    if (pthread_create(&pt, NULL, producer_thread, &parg) != 0) return 1;

    // Start consumers which will report NULL-before-finished
    volatile int finished_announced = 0;
    volatile int null_before_finish = 0;

    pthread_t consumers[K];
    consumer_pre_finish_args_t cargs[K];
    for (int i = 0; i < K; ++i) {
        cargs[i] = (consumer_pre_finish_args_t){
            .q=&q, .gate=&gate, .res=&res,
            .finished_announced=&finished_announced,
            .null_before_finish=&null_before_finish
        };
        if (pthread_create(&consumers[i], NULL, consumer_thread_pre_finish, &cargs[i]) != 0) return 1;
    }

    // Wait for producer to finish pushing, then allow consumers to drain and block
    pthread_join(pt, NULL);
    tiny_sleep_us(10000, 20000); // give consumers time to drain and reach empty state

    // Up to now, consumers must not have observed NULL
    if (null_before_finish) {
        fprintf(stderr,"consumer observed NULL before signal_finished()\n");
        return 1;
    }

    // Announce and signal finished now; consumers may return NULL legitimately
    finished_announced = 1;
    consumer_producer_signal_finished(&q);

    // Join consumers
    for (int i = 0; i < K; ++i) pthread_join(consumers[i], NULL);

    // At this point, all items should have been seen exactly once
    int ok = results_ok(&res) && (null_before_finish == 0) && (consumer_producer_wait_finished(&q) == 0);

    results_destroy(&res); start_barrier_destroy(&gate); consumer_producer_destroy(&q);
    return ok ? 0 : 1;
}

// ======================================================================
// 12) test_finished_wakes_all_consumers
// Several consumers sleep on empty; signal_finished() should wake them all,
// each should return NULL and exit.
// ======================================================================
static int test_finished_wakes_all_consumers(void) {
    const int K = 5; // multiple consumers
    consumer_producer_t q; const char* err = consumer_producer_init(&q, 4);
    if (err) { fprintf(stderr,"init error: %s\n", err); return 1; }

    start_barrier_t gate; start_barrier_init(&gate, K);
    volatile int exited = 0;

    pthread_t th[K];
    consumer_exit_counter_args_t args[K];
    for (int i = 0; i < K; ++i) {
        args[i] = (consumer_exit_counter_args_t){ .q=&q, .gate=&gate, .exited_counter=&exited };
        if (pthread_create(&th[i], NULL, consumer_exit_counter_thread, &args[i]) != 0) return 1;
    }

    // Give them time to block on empty queue
    tiny_sleep_us(10000, 20000);

    // Signal finished: all should wake and return NULL
    consumer_producer_signal_finished(&q);

    for (int i = 0; i < K; ++i) pthread_join(th[i], NULL);

    int ok = (exited == K) && (consumer_producer_wait_finished(&q) == 0);

    start_barrier_destroy(&gate); consumer_producer_destroy(&q);
    return ok ? 0 : 1;
}

// 13) destroy without consumers: do not overfill the queue.
// Push up to capacity items, then destroy. Should not crash.
static int test_destroy_releases_items_no_crash(void) {
    const int CAP = 8;
    consumer_producer_t q; 
    const char* err = consumer_producer_init(&q, CAP);
    if (err) { fprintf(stderr,"init error: %s\n", err); return 1; }

    // Push at most 'CAP' items so put() never blocks.
    for (int i = 0; i < CAP; ++i) {
        char* s = make_item_number(i);
        const char* perr = consumer_producer_put(&q, s);
        if (perr != NULL) { 
            // Should not happen here; on failure the caller owns the string
            free(s); 
            fprintf(stderr,"unexpected put failure at %d\n", i);
            consumer_producer_destroy(&q);
            return 1; 
        }
    }

    // No consumers: queue still holds 'CAP' items.
    // Destroy should free remaining items internally and not crash.
    consumer_producer_destroy(&q);
    return 0;
}




int main(void) {
    RUN_TEST(test_parallel_put_get);
    RUN_TEST(test_stress);
    RUN_TEST(test_multiple_producers);
    RUN_TEST(test_multiple_consumers);
    RUN_TEST(test_capacity_one_high_contention);

    // New tests (6-10)
    RUN_TEST(test_wraparound_indices);
    RUN_TEST(test_finish_without_items);
    RUN_TEST(test_producer_blocked_then_finished);
    RUN_TEST(test_double_signal_finished_idempotent);
    RUN_TEST(test_wait_finished_blocks_until_empty);

    RUN_TEST(test_no_null_before_finished);        // 11
    RUN_TEST(test_finished_wakes_all_consumers);   // 12
    RUN_TEST(test_destroy_releases_items_no_crash);// 13

    // Summary
    if (g_failed_tests == 0) {
        printf(CGREEN "\nAll %d tests passed successfully!\n" CRESET, g_total_tests);
    } else {
        printf(CRED "\n%d/%d tests failed ❌\n" CRESET, g_failed_tests, g_total_tests);
    }
    return (g_failed_tests == 0) ? 0 : 1;
}

