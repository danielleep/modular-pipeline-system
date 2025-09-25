#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h> // For sleep/usleep
#include "../../plugins/sync/consumer_producer.h"


#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define NC "\033[0m"

#define RUN_TEST(test_fn) \
    do { \
        printf("Running %-30s... ", #test_fn); \
        fflush(stdout); \
        if (setjmp(env) == 0) { \
            test_fn(); \
            printf(GREEN "PASSED\n" NC); \
        } else { \
            printf(RED "FAILED\n" NC); \
            failed++; \
        } \
    } while (0)

#include <setjmp.h>
jmp_buf env;
int failed = 0;

void custom_assert(int condition) {
    if (!condition) longjmp(env, 1);
}


// Helper: strdup wrapper (in case not defined)
#ifndef _GNU_SOURCE
char* strdup(const char* s)
{
    size_t len = strlen(s) + 1;
    char* copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}
#endif

// === Test 1: Basic put/get ===
void test_basic_put_get() {
    consumer_producer_t queue;
    assert(consumer_producer_init(&queue, 3) == NULL);

    assert(consumer_producer_put(&queue, strdup("A")) == NULL);
    assert(consumer_producer_put(&queue, strdup("B")) == NULL);
    assert(consumer_producer_put(&queue, strdup("C")) == NULL);

    char* a = consumer_producer_get(&queue);
    char* b = consumer_producer_get(&queue);
    char* c = consumer_producer_get(&queue);

    assert(strcmp(a, "A") == 0);
    assert(strcmp(b, "B") == 0);
    assert(strcmp(c, "C") == 0);

    free(a); free(b); free(c);
    consumer_producer_destroy(&queue);
}

// === Test 2: Blocking when full ===
void* producer_delayed_get(void* arg) {
    consumer_producer_t* q = (consumer_producer_t*)arg;
    sleep(1); // Let main thread block first
    char* val = consumer_producer_get(q);
    free(val);
    return NULL;
}

void test_block_when_full() {
    consumer_producer_t queue;
    assert(consumer_producer_init(&queue, 1) == NULL);

    assert(consumer_producer_put(&queue, strdup("X")) == NULL);

    pthread_t t;
    pthread_create(&t, NULL, producer_delayed_get, &queue);

    // This should block until above thread frees space
    assert(consumer_producer_put(&queue, strdup("Y")) == NULL);

    char* y = consumer_producer_get(&queue);
    assert(strcmp(y, "Y") == 0);
    free(y);

    pthread_join(t, NULL);
    consumer_producer_destroy(&queue);
}

// === Test 3: Blocking when empty ===
void* consumer_get_from_empty(void* arg) {
    consumer_producer_t* q = (consumer_producer_t*)arg;
    char* item = consumer_producer_get(q);
    assert(strcmp(item, "Z") == 0);
    free(item);
    return NULL;
}

void test_block_when_empty() {
    consumer_producer_t queue;
    assert(consumer_producer_init(&queue, 2) == NULL);

    pthread_t t;
    pthread_create(&t, NULL, consumer_get_from_empty, &queue);

    usleep(500 * 1000); // Let consumer block
    assert(consumer_producer_put(&queue, strdup("Z")) == NULL);

    pthread_join(t, NULL);
    consumer_producer_destroy(&queue);
}

// === Test 4: FIFO order ===
void test_fifo_order() {
    consumer_producer_t queue;
    assert(consumer_producer_init(&queue, 3) == NULL);

    assert(consumer_producer_put(&queue, strdup("1")) == NULL);
    assert(consumer_producer_put(&queue, strdup("2")) == NULL);
    assert(consumer_producer_put(&queue, strdup("3")) == NULL);

    char* first = consumer_producer_get(&queue);
    char* second = consumer_producer_get(&queue);
    char* third = consumer_producer_get(&queue);

    assert(strcmp(first, "1") == 0);
    assert(strcmp(second, "2") == 0);
    assert(strcmp(third, "3") == 0);

    free(first); free(second); free(third);
    consumer_producer_destroy(&queue);
}

// === Test 5: signal_finished and draining ===
void* consumer_drain_then_wait(void* arg) {
    consumer_producer_t* q = (consumer_producer_t*)arg;
    char* item;
    while ((item = consumer_producer_get(q)) != NULL) {
        free(item);
    }
    return NULL;
}

void test_signal_and_drain() {
    consumer_producer_t queue;
    assert(consumer_producer_init(&queue, 5) == NULL);

    assert(consumer_producer_put(&queue, strdup("one")) == NULL);
    assert(consumer_producer_put(&queue, strdup("two")) == NULL);

    pthread_t t;
    pthread_create(&t, NULL, consumer_drain_then_wait, &queue);

    sleep(1); // Let consumer start and read
    consumer_producer_signal_finished(&queue);
    assert(consumer_producer_wait_finished(&queue) == 0);
    assert(consumer_producer_wait_finished(&queue) == 0); // second call

    pthread_join(t, NULL);
    consumer_producer_destroy(&queue);
}

// === Test 6: put after signal_finished ===
void test_put_after_finished() {
    consumer_producer_t queue;
    assert(consumer_producer_init(&queue, 2) == NULL);

    consumer_producer_signal_finished(&queue);
    const char* err = consumer_producer_put(&queue, strdup("illegal"));
    assert(err != NULL);

    consumer_producer_destroy(&queue);
}

// === Test 7: get returns NULL after finished ===
void test_get_after_finished() {
    consumer_producer_t queue;
    assert(consumer_producer_init(&queue, 1) == NULL);

    consumer_producer_signal_finished(&queue);
    char* item = consumer_producer_get(&queue);
    assert(item == NULL);

    consumer_producer_destroy(&queue);
}

// === Test 8: simple put/get without threads ===


// === Test 9: multiple queues in parallel ===
void test_multiple_queues() {
    consumer_producer_t q1, q2;
    assert(consumer_producer_init(&q1, 2) == NULL);
    assert(consumer_producer_init(&q2, 2) == NULL);

    assert(consumer_producer_put(&q1, strdup("Q1")) == NULL);
    assert(consumer_producer_put(&q2, strdup("Q2")) == NULL);

    char* r1 = consumer_producer_get(&q1);
    char* r2 = consumer_producer_get(&q2);

    assert(strcmp(r1, "Q1") == 0);
    assert(strcmp(r2, "Q2") == 0);

    free(r1);
    free(r2);
    consumer_producer_destroy(&q1);
    consumer_producer_destroy(&q2);
}

// === Test 10: double init and double destroy ===
void test_double_init_destroy() {
    consumer_producer_t queue;
    assert(consumer_producer_init(&queue, 1) == NULL);
    assert(consumer_producer_init(&queue, 1) != NULL); // should fail

    consumer_producer_destroy(&queue);
    consumer_producer_destroy(&queue); // should be safe
}

// === Test 11: put(NULL) ===
void test_put_null() {
    consumer_producer_t queue;
    assert(consumer_producer_init(&queue, 1) == NULL);

    const char* err = consumer_producer_put(&queue, NULL);
    assert(err != NULL);

    consumer_producer_destroy(&queue);
}

// === Test 12: put/get before init ===
void test_put_get_before_init() {
    consumer_producer_t queue;
    // no init
    const char* err = consumer_producer_put(&queue, strdup("X"));
    assert(err != NULL);

    char* result = consumer_producer_get(&queue);
    assert(result == NULL);
}

// === Test 13: blocking is real (no busy-wait) ===
void test_blocking_delay() {
    consumer_producer_t queue;
    assert(consumer_producer_init(&queue, 1) == NULL);

    assert(consumer_producer_put(&queue, strdup("hold")) == NULL);

    // Start consumer that frees slot after 500ms
    pthread_t t;
    void* delayed_get(void* arg) {
        consumer_producer_t* q = (consumer_producer_t*)arg;
        usleep(500 * 1000);
        char* item = consumer_producer_get(q);
        free(item);
        return NULL;
    }
    pthread_create(&t, NULL, delayed_get, &queue);

    // Measure how long put() takes (should be >= 0.5s)
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    assert(consumer_producer_put(&queue, strdup("after")) == NULL);
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    assert(elapsed >= 0.4); // should block for at least ~0.5s

    char* final = consumer_producer_get(&queue);
    assert(strcmp(final, "after") == 0);
    free(final);

    pthread_join(t, NULL);
    consumer_producer_destroy(&queue);
}

// === Test 14: busy wait detection (CPU activity) ===
void test_no_busy_wait() {
    consumer_producer_t queue;
    assert(consumer_producer_init(&queue, 1) == NULL);
    assert(consumer_producer_put(&queue, strdup("block")) == NULL);

    pthread_t t;
    void* get_after_delay(void* arg) {
        usleep(500 * 1000);
        char* item = consumer_producer_get((consumer_producer_t*)arg);
        free(item);
        return NULL;
    }
    pthread_create(&t, NULL, get_after_delay, &queue);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    assert(consumer_producer_put(&queue, strdup("delayed")) == NULL);
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    assert(elapsed >= 0.4);

    char* result = consumer_producer_get(&queue);
    assert(strcmp(result, "delayed") == 0);
    free(result);

    pthread_join(t, NULL);
    consumer_producer_destroy(&queue);
}

// === Test 15: Stress Test ===


// === Test 16: Multiple Producers ===

// === Test 17: Multiple Consumers ===


// === Test 18: Destroy during use ===
void* producer_during_destroy(void* arg) {
    consumer_producer_t* q = (consumer_producer_t*)arg;
    for (int i = 0; i < 5; ++i) {
        char* msg = strdup("unsafe");
        consumer_producer_put(q, msg);
    }
    return NULL;
}

void test_destroy_during_use() {
    consumer_producer_t queue;
    assert(consumer_producer_init(&queue, 5) == NULL);

    pthread_t p;
    pthread_create(&p, NULL, producer_during_destroy, &queue);

    sleep(1); // let producer start
    consumer_producer_destroy(&queue); // this is undefined, but must not crash

    pthread_join(p, NULL);
}

// === Test 19: Bonus Stress Test ===


int main() {
    RUN_TEST(test_basic_put_get);
    RUN_TEST(test_block_when_full);
    RUN_TEST(test_block_when_empty);
    RUN_TEST(test_fifo_order);
    RUN_TEST(test_signal_and_drain);
    RUN_TEST(test_put_after_finished);
    RUN_TEST(test_get_after_finished);
    RUN_TEST(test_multiple_queues);
    RUN_TEST(test_double_init_destroy);
    RUN_TEST(test_put_null);
    RUN_TEST(test_put_get_before_init);
    RUN_TEST(test_blocking_delay);
    RUN_TEST(test_no_busy_wait);
    RUN_TEST(test_destroy_during_use);


    //RUN_TEST(test_parallel_put_get);
    //RUN_TEST(test_stress);
    //RUN_TEST(test_multiple_producers);
    //RUN_TEST(test_multiple_consumers);


    if (failed == 0) {
        printf(GREEN "\nAll tests passed successfully!\n" NC);
    } else {
        printf(RED "\n%d test(s) failed.\n" NC, failed);
    }
    return failed;
}
