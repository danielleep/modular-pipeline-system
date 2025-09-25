// tests/consumer producer/extra_integration_tests.c
// Extra integration tests for consumer_producer (part: 1-3)
//
// Tests included:
// 1) test_many_wait_finished_waiters
// 2) test_finish_immediate_with_waiters
// 3) test_finish_while_producers_alive
//
// Notes:
// - Pure C11, portable POSIX sleep via nanosleep.
// - No Hebrew inside code (comments are English-only).

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#include "consumer_producer.h"  // include via -I flag

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
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { /* retry */ }
}

static char* make_item_number(int number) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%d", number);
    char* s = (char*)malloc((size_t)n + 1);
    if (!s) return NULL;
    memcpy(s, buf, (size_t)n + 1);
    return s;
}

static int parse_item_number(const char* s) {
    return (int)strtol(s, NULL, 10);
}

// ---------- Simple start barrier (portable) ----------
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  cv;
    int count;
    int total;
} start_barrier_t;

static void start_barrier_init(start_barrier_t* b, int total) {
    pthread_mutex_init(&b->m, NULL);
    pthread_cond_init(&b->cv, NULL);
    b->count = 0; b->total = total;
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
    int N;                // expected unique items
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
    free(r->seen); r->seen = NULL;
    pthread_mutex_destroy(&r->m);
}
static void results_record(results_t* r, int token) {
    pthread_mutex_lock(&r->m);
    if (token < 0 || token >= r->N) {
        r->out_of_range++;
    } else if (r->seen[token] == 0) {
        r->seen[token] = 1; r->received++;
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
    return (r->received == r->N) && (r->duplicates == 0) &&
           (r->out_of_range == 0) && (r->errors == 0);
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
    results_t* res;     // for counting put() failures (should be 0 normally)
    int with_delays;    // optional small random delays
} producer_args_t;

typedef struct {
    consumer_producer_t* q;
    start_barrier_t* gate;
    results_t* res;
    int with_delays;
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
        if (err != NULL) { free(item); results_add_error(a->res); }
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
        if (!s) break; // finished & empty
        int token = parse_item_number(s);
        results_record(a->res, token);
        free(s);
        if (a->with_delays) tiny_sleep_us(0, 200);
    }
    return NULL;
}

// ---------- Waiter: call wait_finished and store rc ----------
typedef struct {
    consumer_producer_t* q;
    int rc; // result from wait_finished
} waiter_args_t;

static void* waiter_thread(void* arg) {
    waiter_args_t* a = (waiter_args_t*)arg;
    a->rc = consumer_producer_wait_finished(a->q);
    return NULL;
}

// ---------- Single blocking put (used in test #3) ----------
typedef struct {
    consumer_producer_t* q;
    int token;
    const char* err; // NULL on success
} single_put_args_t;

static void* single_put_thread(void* arg) {
    single_put_args_t* a = (single_put_args_t*)arg;
    char* s = make_item_number(a->token);
    a->err = consumer_producer_put(a->q, s);
    if (a->err != NULL) free(s); // caller retains ownership on failure
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
    for (int i = 0; i < n; ++i) (void)pthread_join(arr[i], NULL);
}

// ===== helpers for test #4 (multi-queues with base token offsets) =====
typedef struct {
    consumer_producer_t* q;
    start_barrier_t* gate;
    int producer_id;
    int items_per_producer;
    int base_offset;     // add to produced token ids
    results_t* res;
    int with_delays;
} prod_base_args_t;

static void* producer_thread_with_base(void* arg) {
    prod_base_args_t* a = (prod_base_args_t*)arg;
    start_barrier_wait(a->gate);
    for (int i = 0; i < a->items_per_producer; ++i) {
        int token = a->base_offset + a->producer_id * a->items_per_producer + i;
        char* item = make_item_number(token);
        if (!item) { results_add_error(a->res); continue; }
        const char* err = consumer_producer_put(a->q, item);
        if (err != NULL) { free(item); results_add_error(a->res); }
        if (a->with_delays) tiny_sleep_us(0, 200);
    }
    return NULL;
}

typedef struct {
    consumer_producer_t* q;
    start_barrier_t* gate;
    results_t* res;
    int with_delays;
    int base_offset;     // subtract before recording, to map into [0..N-1]
} cons_base_args_t;

static void* consumer_thread_with_base(void* arg) {
    cons_base_args_t* a = (cons_base_args_t*)arg;
    start_barrier_wait(a->gate);
    for (;;) {
        char* s = consumer_producer_get(a->q);
        if (!s) break;
        int token = parse_item_number(s) - a->base_offset;
        results_record(a->res, token);   // out_of_range will catch cross-talk
        free(s);
        if (a->with_delays) tiny_sleep_us(0, 200);
    }
    return NULL;
}

// ===== helper for test #5 (single get + exit counter) =====
typedef struct {
    consumer_producer_t* q;
    start_barrier_t* gate;
    volatile int* exited_counter;
} single_get_counter_args_t;

static void* consumer_single_get_counter(void* arg) {
    single_get_counter_args_t* a = (single_get_counter_args_t*)arg;
    start_barrier_wait(a->gate);
    char* s = consumer_producer_get(a->q);
    if (s) free(s); // should be NULL in this test, but safe anyway
    __sync_fetch_and_add(a->exited_counter, 1);
    return NULL;
}


// ======================================================================
// 1) test_many_wait_finished_waiters
// Several waiters call wait_finished() concurrently; all must return 0.
// ======================================================================
static int test_many_wait_finished_waiters(void) {
    srand((unsigned int)time(NULL) ^ 0x12345678u);
    const int capacity = 8;
    const int M = 3, K = 3;         // producers/consumers
    const int W = 5;                // wait_finished waiters
    const int ITEMS_PER_PRODUCER = 1200;
    const int N = M * ITEMS_PER_PRODUCER;

    consumer_producer_t q;
    const char* err = consumer_producer_init(&q, capacity);
    if (err) { fprintf(stderr, "init error: %s\n", err); return 1; }

    results_t res;
    if (results_init(&res, N) != 0) { consumer_producer_destroy(&q); return 1; }

    start_barrier_t gate;
    start_barrier_init(&gate, M + K);

    pthread_t producers[M], consumers[K], waiters[W];
    producer_args_t pargs[M]; consumer_args_t cargs[K]; waiter_args_t wargs[W];
    void* pargs_ptrs[M]; void* cargs_ptrs[K];

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

    // Launch W waiter threads in parallel (they will block until finished+drained)
    for (int i = 0; i < W; ++i) {
        wargs[i].q = &q; wargs[i].rc = -2;
        if (pthread_create(&waiters[i], NULL, waiter_thread, &wargs[i]) != 0) return 1;
    }

    join_thread_array(producers, M);
    consumer_producer_signal_finished(&q);
    join_thread_array(consumers, K);

    // Join all waiters and assert rc==0
    int all_ok = 1;
    for (int i = 0; i < W; ++i) {
        (void)pthread_join(waiters[i], NULL);
        if (wargs[i].rc != 0) { all_ok = 0; }
    }

    if (consumer_producer_wait_finished(&q) != 0) all_ok = 0;

    results_print(&res, "many_wait_finished_waiters");
    int ok = all_ok && results_ok(&res);

    results_destroy(&res);
    start_barrier_destroy(&gate);
    consumer_producer_destroy(&q);
    return ok ? 0 : 1;
}

// ======================================================================
// 2) test_finish_immediate_with_waiters
// No producers; consumers are sleeping on empty; signal_finished() should
// wake all consumers; wait_finished() waiters should return immediately.
// ======================================================================
static int test_finish_immediate_with_waiters(void) {
    const int capacity = 4;
    const int K = 3; // consumers
    const int W = 3; // waiters

    consumer_producer_t q;
    const char* err = consumer_producer_init(&q, capacity);
    if (err) { fprintf(stderr, "init error: %s\n", err); return 1; }

    // Launch K consumers that will block on empty (they will call get once and exit on NULL)
    start_barrier_t gate; start_barrier_init(&gate, K);
    pthread_t consumers[K];
    consumer_args_t cargs[K]; void* cptrs[K];
    results_t dummy_res; (void)results_init(&dummy_res, 0); // not used here

    for (int i = 0; i < K; ++i) {
        cargs[i] = (consumer_args_t){ .q=&q, .gate=&gate, .res=&dummy_res, .with_delays=0 };
        cptrs[i] = &cargs[i];
    }
    if (create_thread_array(consumers, K, consumer_thread, cptrs) != 0) return 1;

    // Launch W waiters that will block on wait_finished()
    pthread_t waiters[W];
    waiter_args_t wargs[W];
    for (int i = 0; i < W; ++i) {
        wargs[i].q = &q; wargs[i].rc = -2;
        if (pthread_create(&waiters[i], NULL, waiter_thread, &wargs[i]) != 0) return 1;
    }

    // Give a short time for everyone to block
    tiny_sleep_us(10000, 20000);

    // Signal finished: all consumers should wake and return NULL;
    // all waiters should return 0 immediately (queue already empty).
    consumer_producer_signal_finished(&q);

    join_thread_array(consumers, K);

    int all_waiters_ok = 1;
    for (int i = 0; i < W; ++i) {
        (void)pthread_join(waiters[i], NULL);
        if (wargs[i].rc != 0) { all_waiters_ok = 0; }
    }

    int ok = all_waiters_ok && (consumer_producer_wait_finished(&q) == 0);

    start_barrier_destroy(&gate);
    results_destroy(&dummy_res);
    consumer_producer_destroy(&q);
    return ok ? 0 : 1;
}

// ======================================================================
// 3) test_finish_while_producers_alive
// Capacity=1: second put blocks; signal_finished arrives while blocked.
// That put must still complete; new puts after finished must be rejected.
// ======================================================================
static int test_finish_while_producers_alive(void) {
    consumer_producer_t q;
    const char* err = consumer_producer_init(&q, 1);
    if (err) { fprintf(stderr, "init error: %s\n", err); return 1; }

    // Fill the queue with one item so next put will block.
    char* first = make_item_number(0);
    if (!first) { consumer_producer_destroy(&q); return 1; }
    if (consumer_producer_put(&q, first) != NULL) { free(first); consumer_producer_destroy(&q); return 1; }

    // Start a producer that will attempt to put token 1 and block until space frees.
    pthread_t pth;
    single_put_args_t pa = { .q=&q, .token=1, .err=NULL };
    if (pthread_create(&pth, NULL, single_put_thread, &pa) != 0) { consumer_producer_destroy(&q); return 1; }

    // Give it a moment to block.
    tiny_sleep_us(10000, 20000);

    // Signal finished WHILE the producer is blocked.
    consumer_producer_signal_finished(&q);

    // Consumer takes one item to free space (should be token 0).
    char* got = consumer_producer_get(&q);
    if (!got) { fprintf(stderr, "get returned NULL unexpectedly\n"); return 1; }
    int t0 = parse_item_number(got); free(got);
    if (t0 != 0) { fprintf(stderr, "expected token 0 first, got %d\n", t0); return 1; }

    // The blocked put (token 1) must now complete successfully.
    (void)pthread_join(pth, NULL);
    if (pa.err != NULL) { fprintf(stderr, "blocked put failed after finished\n"); return 1; }

    // Any NEW put after finished must be rejected immediately.
    char* after = make_item_number(2);
    const char* perr = consumer_producer_put(&q, after);
    if (perr == NULL) { fprintf(stderr, "new put after finished unexpectedly succeeded\n"); return 1; }
    free(after); // caller retains ownership on failure

    // Drain remaining item (should be token 1) and verify.
    char* s = consumer_producer_get(&q);
    if (!s) { fprintf(stderr, "expected one more item (token 1)\n"); return 1; }
    int t1 = parse_item_number(s); free(s);
    if (t1 != 1) { fprintf(stderr, "expected token 1, got %d\n", t1); return 1; }

    // Now finished+empty: next get must return NULL; wait_finished() must return 0.
    if (consumer_producer_get(&q) != NULL) { fprintf(stderr, "expected NULL after drain+finished\n"); return 1; }
    int ok = (consumer_producer_wait_finished(&q) == 0);

    consumer_producer_destroy(&q);
    return ok ? 0 : 1;
}

// ======================================================================
// 4) test_multi_queues_isolation
// Two independent queues run concurrently; no cross-contamination.
// We use base offsets: q1 tokens in [0..N1-1], q2 tokens in [BASE..BASE+N2-1].
// Consumers of q2 subtract BASE before recording; any cross-queue mix
// will be detected as out_of_range in the wrong results bucket.
// ======================================================================
static int test_multi_queues_isolation(void) {
    srand((unsigned int)time(NULL) ^ 0xCAFEBABEu);

    // Queue 1 parameters
    const int cap1 = 4, M1 = 2, K1 = 2, ITEMS1 = 800, N1 = M1 * ITEMS1;
    // Queue 2 parameters
    const int cap2 = 8, M2 = 3, K2 = 3, ITEMS2 = 600, N2 = M2 * ITEMS2;
    const int BASE2 = 100000; // far away from q1 range

    consumer_producer_t q1, q2;
    const char* err;
    if ((err = consumer_producer_init(&q1, cap1)) != NULL) { fprintf(stderr,"init q1: %s\n", err); return 1; }
    if ((err = consumer_producer_init(&q2, cap2)) != NULL) { fprintf(stderr,"init q2: %s\n", err); consumer_producer_destroy(&q1); return 1; }

    results_t res1, res2;
    if (results_init(&res1, N1) != 0 || results_init(&res2, N2) != 0) {
        consumer_producer_destroy(&q1); consumer_producer_destroy(&q2); return 1;
    }

    start_barrier_t gate1, gate2;
    start_barrier_init(&gate1, M1 + K1);
    start_barrier_init(&gate2, M2 + K2);

    // Threads and args for q1
    pthread_t prod1[M1], cons1[K1];
    prod_base_args_t parg1[M1];
    cons_base_args_t carg1[K1];

    // Threads and args for q2
    pthread_t prod2[M2], cons2[K2];
    prod_base_args_t parg2[M2];
    cons_base_args_t carg2[K2];

    // Launch q1 producers/consumers
    for (int p=0; p<M1; ++p) {
        parg1[p] = (prod_base_args_t){ .q=&q1, .gate=&gate1, .producer_id=p,
                                       .items_per_producer=ITEMS1, .base_offset=0,
                                       .res=&res1, .with_delays=1 };
        if (pthread_create(&prod1[p], NULL, producer_thread_with_base, &parg1[p]) != 0) return 1;
    }
    for (int c=0; c<K1; ++c) {
        carg1[c] = (cons_base_args_t){ .q=&q1, .gate=&gate1, .res=&res1,
                                       .with_delays=1, .base_offset=0 };
        if (pthread_create(&cons1[c], NULL, consumer_thread_with_base, &carg1[c]) != 0) return 1;
    }

    // Launch q2 producers/consumers (run concurrently with q1)
    for (int p=0; p<M2; ++p) {
        parg2[p] = (prod_base_args_t){ .q=&q2, .gate=&gate2, .producer_id=p,
                                       .items_per_producer=ITEMS2, .base_offset=BASE2,
                                       .res=&res2, .with_delays=1 };
        if (pthread_create(&prod2[p], NULL, producer_thread_with_base, &parg2[p]) != 0) return 1;
    }
    for (int c=0; c<K2; ++c) {
        carg2[c] = (cons_base_args_t){ .q=&q2, .gate=&gate2, .res=&res2,
                                       .with_delays=1, .base_offset=BASE2 };
        if (pthread_create(&cons2[c], NULL, consumer_thread_with_base, &carg2[c]) != 0) return 1;
    }

    // Finish each queue independently
    for (int p=0; p<M1; ++p) (void)pthread_join(prod1[p], NULL);
    consumer_producer_signal_finished(&q1);
    for (int p=0; p<M2; ++p) (void)pthread_join(prod2[p], NULL);
    consumer_producer_signal_finished(&q2);

    for (int c=0; c<K1; ++c) (void)pthread_join(cons1[c], NULL);
    for (int c=0; c<K2; ++c) (void)pthread_join(cons2[c], NULL);

    // Both queues must complete
    int ok = 1;
    if (consumer_producer_wait_finished(&q1) != 0) ok = 0;
    if (consumer_producer_wait_finished(&q2) != 0) ok = 0;

    // Isolation check: no out_of_range/duplicates/errors in either results
    results_print(&res1, "multi_queues_q1");
    results_print(&res2, "multi_queues_q2");
    ok = ok && results_ok(&res1) && results_ok(&res2);

    results_destroy(&res1); results_destroy(&res2);
    start_barrier_destroy(&gate1); start_barrier_destroy(&gate2);
    consumer_producer_destroy(&q1); consumer_producer_destroy(&q2);
    return ok ? 0 : 1;
}

// ======================================================================
// 5) test_finished_twice_consumers_asleep
// Consumers are sleeping on empty; signal_finished() called twice.
// All consumers must wake and exit (return NULL). Idempotent behavior.
// ======================================================================
static int test_finished_twice_consumers_asleep(void) {
    const int K = 5;
    consumer_producer_t q;
    const char* err = consumer_producer_init(&q, 4);
    if (err) { fprintf(stderr,"init error: %s\n", err); return 1; }

    start_barrier_t gate; start_barrier_init(&gate, K);
    volatile int exited = 0;

    pthread_t th[K];
    single_get_counter_args_t args[K];
    for (int i = 0; i < K; ++i) {
        args[i] = (single_get_counter_args_t){ .q=&q, .gate=&gate, .exited_counter=&exited };
        if (pthread_create(&th[i], NULL, consumer_single_get_counter, &args[i]) != 0) return 1;
    }

    // Let them block on empty queue
    tiny_sleep_us(10000, 20000);

    // Call signal_finished twice; function is idempotent.
    consumer_producer_signal_finished(&q);
    consumer_producer_signal_finished(&q);

    for (int i = 0; i < K; ++i) (void)pthread_join(th[i], NULL);

    int ok = (exited == K) && (consumer_producer_wait_finished(&q) == 0);

    start_barrier_destroy(&gate);
    consumer_producer_destroy(&q);
    return ok ? 0 : 1;
}



// ======================================================================
// main runner
// ======================================================================
int main(void) {
    RUN_TEST(test_many_wait_finished_waiters);
    RUN_TEST(test_finish_immediate_with_waiters);
    RUN_TEST(test_finish_while_producers_alive);

    // New:
    RUN_TEST(test_multi_queues_isolation);          // 4
    RUN_TEST(test_finished_twice_consumers_asleep); // 5

        // Summary
    if (g_failed_tests == 0) {
        printf(CGREEN "\nAll %d tests passed successfully!\n" CRESET, g_total_tests);
    } else {
        printf(CRED "\n%d/%d tests failed ❌\n" CRESET, g_failed_tests, g_total_tests);
    }
    return (g_failed_tests == 0) ? 0 : 1;
}

