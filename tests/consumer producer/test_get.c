#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#include "../../plugins/sync/consumer_producer.h"

// Output colors
#define RED     "\033[0;31m"
#define GREEN   "\033[0;32m"
#define NC      "\033[0m"

#define TEST_PASS(msg) printf(GREEN "[PASS] " NC msg "\n")
#define TEST_FAIL(msg) do { printf(RED "[FAIL] " NC msg "\n"); exit(1); } while(0)

void test_get_after_put() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 2);

    consumer_producer_put(&queue, strdup("hello"));
    char* result = consumer_producer_get(&queue);

    if (!result || strcmp(result, "hello") != 0)
        TEST_FAIL("Get after put failed");

    free(result);
    consumer_producer_destroy(&queue);
    TEST_PASS("Get after put works");
}

void test_fifo_order() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 3);

    consumer_producer_put(&queue, strdup("one"));
    consumer_producer_put(&queue, strdup("two"));
    consumer_producer_put(&queue, strdup("three"));

    char* r1 = consumer_producer_get(&queue);
    char* r2 = consumer_producer_get(&queue);
    char* r3 = consumer_producer_get(&queue);

    if (!r1 || strcmp(r1, "one") != 0 ||
        !r2 || strcmp(r2, "two") != 0 ||
        !r3 || strcmp(r3, "three") != 0)
        TEST_FAIL("FIFO order broken");

    free(r1); free(r2); free(r3);
    consumer_producer_destroy(&queue);
    TEST_PASS("FIFO order works");
}

typedef struct {
    consumer_producer_t* queue;
    char** result;
    struct timespec* start;
    struct timespec* end;
} get_block_args;

void* blocking_get_thread(void* arg) {
    get_block_args* a = (get_block_args*)arg;
    clock_gettime(CLOCK_MONOTONIC, a->start);
    *(a->result) = consumer_producer_get(a->queue);
    clock_gettime(CLOCK_MONOTONIC, a->end);
    return NULL;
}

void test_blocking_get_on_empty_queue() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 1);

    pthread_t thread;
    char* result = NULL;
    struct timespec start, end;
    get_block_args args = { .queue = &queue, .result = &result, .start = &start, .end = &end };

    pthread_create(&thread, NULL, blocking_get_thread, &args);

    sleep(1); // ensure thread blocks

    consumer_producer_put(&queue, strdup("data"));

    pthread_join(thread, NULL);

    double elapsed = (args.end->tv_sec - args.start->tv_sec) +
                     (args.end->tv_nsec - args.start->tv_nsec) / 1e9;

    if (!result || strcmp(result, "data") != 0)
        TEST_FAIL("Blocking get did not receive correct data");

    if (elapsed < 0.5)
        TEST_FAIL("Blocking get returned too fast — possible busy wait");

    free(result);
    consumer_producer_destroy(&queue);
    TEST_PASS("Blocking get on empty queue works (no busy wait)");
}

void test_get_after_signal_finished_with_items() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 2);

    consumer_producer_put(&queue, strdup("one"));
    consumer_producer_signal_finished(&queue);

    char* r1 = consumer_producer_get(&queue);
    char* r2 = consumer_producer_get(&queue);

    if (!r1 || strcmp(r1, "one") != 0)
        TEST_FAIL("Get after signal_finished (with item) failed");

    if (r2 != NULL)
        TEST_FAIL("Get after queue is empty should return NULL");

    free(r1);
    consumer_producer_destroy(&queue);
    TEST_PASS("Get after signal_finished (with items) works");
}

void test_get_after_signal_finished_empty() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 1);

    consumer_producer_signal_finished(&queue);
    char* result = consumer_producer_get(&queue);

    if (result != NULL)
        TEST_FAIL("Get after signal_finished on empty queue should return NULL");

    consumer_producer_destroy(&queue);
    TEST_PASS("Get after signal_finished (empty queue) returns NULL");
}

void test_get_on_uninitialized_queue() {
    consumer_producer_t queue;
    memset(&queue, 0, sizeof(queue));

    char* result = consumer_producer_get(&queue);
    if (result != NULL)
        TEST_FAIL("Get on uninitialized queue should return NULL");

    TEST_PASS("Get on uninitialized queue returns NULL");
}

typedef struct {
    consumer_producer_t* queue;
    int* wait_finished_done;
} wait_thread_args;

void* wait_finished_thread(void* arg) {
    wait_thread_args* a = (wait_thread_args*)arg;
    consumer_producer_wait_finished(a->queue);
    *(a->wait_finished_done) = 1;
    return NULL;
}

void test_wait_finished_blocks_until_empty() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 1);
    consumer_producer_put(&queue, strdup("A"));
    consumer_producer_signal_finished(&queue);

    int finished_flag = 0;
    pthread_t thread;
    wait_thread_args args = { .queue = &queue, .wait_finished_done = &finished_flag };

    pthread_create(&thread, NULL, wait_finished_thread, &args);

    sleep(1); // give it time to block
    if (finished_flag)
        TEST_FAIL("wait_finished returned before queue was emptied");

    char* item = consumer_producer_get(&queue);
    free(item);

    pthread_join(thread, NULL);
    if (!finished_flag)
        TEST_FAIL("wait_finished did not return after queue emptied");

    consumer_producer_destroy(&queue);
    TEST_PASS("wait_finished blocks until queue is empty");
}

int main() {
    printf("=== Testing consumer_producer_get ===\n");
    test_get_after_put();
    test_fifo_order();
    test_blocking_get_on_empty_queue();
    test_get_after_signal_finished_with_items();
    test_get_after_signal_finished_empty();
    test_get_on_uninitialized_queue();
    test_wait_finished_blocks_until_empty();
    printf(GREEN "All get tests passed.\n" NC);
    return 0;
}
