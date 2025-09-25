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

void test_put_single_item() {
    consumer_producer_t queue;
    if (consumer_producer_init(&queue, 2) != NULL)
        TEST_FAIL("Initialization failed");

    const char* msg = strdup("hello");
    const char* err = consumer_producer_put(&queue, msg);
    if (err != NULL)
        TEST_FAIL("Failed to put single item");

    if (queue.count != 1)
        TEST_FAIL("Queue count incorrect after put");

    consumer_producer_destroy(&queue);
    TEST_PASS("Put single item works");
}

void test_put_multiple_items() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 3);

    consumer_producer_put(&queue, strdup("one"));
    consumer_producer_put(&queue, strdup("two"));
    consumer_producer_put(&queue, strdup("three"));

    if (queue.count != 3)
        TEST_FAIL("Queue count incorrect after multiple puts");

    consumer_producer_destroy(&queue);
    TEST_PASS("Put multiple items works");
}

void test_put_null_item() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 2);

    const char* err = consumer_producer_put(&queue, NULL);
    if (err == NULL)
        TEST_FAIL("Put with NULL item should fail");

    consumer_producer_destroy(&queue);
    TEST_PASS("Put NULL item fails as expected");
}

void test_put_uninitialized_queue() {
    consumer_producer_t queue;
    memset(&queue, 0, sizeof(queue));

    const char* err = consumer_producer_put(&queue, strdup("test"));
    if (err == NULL)
        TEST_FAIL("Put on uninitialized queue should fail");

    TEST_PASS("Put on uninitialized queue fails as expected");
}

void test_put_after_finished() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 2);

    consumer_producer_signal_finished(&queue);
    const char* err = consumer_producer_put(&queue, strdup("test"));

    if (err == NULL)
        TEST_FAIL("Put after finished should fail");

    consumer_producer_destroy(&queue);
    TEST_PASS("Put after finished fails as expected");
}

// Threading test

typedef struct {
    consumer_producer_t* queue;
    int* flag;
    struct timespec* start;
    struct timespec* end;
} thread_arg_t;

void* blocked_put_thread(void* arg) {
    thread_arg_t* t = (thread_arg_t*)arg;
    *(t->flag) = 1;
    clock_gettime(CLOCK_MONOTONIC, t->start);
    consumer_producer_put(t->queue, strdup("BLOCKED"));
    clock_gettime(CLOCK_MONOTONIC, t->end);
    pthread_exit(NULL);
}

void test_blocking_when_full() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 1);

    // Fill the queue
    consumer_producer_put(&queue, strdup("full"));

    pthread_t thread;
    int started = 0;
    struct timespec start_time, end_time;

    thread_arg_t args = {
        .queue = &queue,
        .flag = &started,
        .start = &start_time,
        .end = &end_time
    };

    pthread_create(&thread, NULL, blocked_put_thread, &args);

    // Wait to ensure thread started and is likely blocking
    sleep(1);

    if (!started)
        TEST_FAIL("Put thread did not start");

    // Unblock the thread by consuming one item
    char* item = consumer_producer_get(&queue);
    free(item);

    pthread_join(thread, NULL);

    // Calculate time difference
    double elapsed = (end_time.tv_sec - start_time.tv_sec)
                   + (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    if (elapsed < 0.9)
        TEST_FAIL("Put did not block as expected (too fast)");

    consumer_producer_destroy(&queue);
    TEST_PASS("Put blocks correctly when full (no busy wait)");
}

int main() {
    printf("=== Testing consumer_producer_put ===\n");
    test_put_single_item();
    test_put_multiple_items();
    test_put_null_item();
    test_put_uninitialized_queue();
    test_put_after_finished();
    test_blocking_when_full();
    printf(GREEN "All put tests passed.\n" NC);
    return 0;
}
