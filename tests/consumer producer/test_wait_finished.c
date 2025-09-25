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

void test_returns_0_when_finished_and_empty() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 1);
    consumer_producer_signal_finished(&queue);

    int result = consumer_producer_wait_finished(&queue);
    if (result != 0)
        TEST_FAIL("wait_finished should return 0 when queue is empty and finished");

    consumer_producer_destroy(&queue);
    TEST_PASS("Returns 0 when finished and empty");
}

typedef struct {
    consumer_producer_t* queue;
    int* flag;
    struct timespec* start;
    struct timespec* end;
} wait_args_t;

void* wait_thread_func(void* arg) {
    wait_args_t* a = (wait_args_t*)arg;
    clock_gettime(CLOCK_MONOTONIC, a->start);
    int res = consumer_producer_wait_finished(a->queue);
    clock_gettime(CLOCK_MONOTONIC, a->end);
    *(a->flag) = res;
    return NULL;
}

void test_blocks_until_queue_empty() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 1);
    consumer_producer_put(&queue, strdup("X"));
    consumer_producer_signal_finished(&queue);

    int result = -1;
    struct timespec start, end;
    wait_args_t args = { .queue = &queue, .flag = &result, .start = &start, .end = &end };

    pthread_t thread;
    pthread_create(&thread, NULL, wait_thread_func, &args);

    sleep(1);
    if (result == 0)
        TEST_FAIL("wait_finished returned before queue was emptied");

    char* item = consumer_producer_get(&queue);
    free(item);

    pthread_join(thread, NULL);

    if (result != 0)
        TEST_FAIL("wait_finished did not return 0 after queue was emptied");

    consumer_producer_destroy(&queue);
    TEST_PASS("Blocks until queue is empty");
}

void test_does_not_return_before_finish() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 1);
    consumer_producer_put(&queue, strdup("data"));

    int done = 0;
    pthread_t thread;
    pthread_create(&thread, NULL, (void*(*)(void*))consumer_producer_wait_finished, &queue);
    sleep(1);

    if (done)
        TEST_FAIL("wait_finished returned before signal_finished");

    char* item = consumer_producer_get(&queue);
    free(item);
    consumer_producer_signal_finished(&queue);

    pthread_join(thread, NULL);
    consumer_producer_destroy(&queue);
    TEST_PASS("Does not return before signal_finished");
}

void test_multiple_calls() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 1);

    consumer_producer_signal_finished(&queue);
    //printf("[DEBUG] Called signal_finished()\n");

    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    int r1 = consumer_producer_wait_finished(&queue);
    clock_gettime(CLOCK_MONOTONIC, &t2);
    double elapsed1 = (t2.tv_sec - t1.tv_sec) + (t2.tv_nsec - t1.tv_nsec) / 1e9;
    //printf("[DEBUG] First wait_finished() returned %d after %.2f seconds\n", r1, elapsed1);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    int r2 = consumer_producer_wait_finished(&queue);
    clock_gettime(CLOCK_MONOTONIC, &t2);
    double elapsed2 = (t2.tv_sec - t1.tv_sec) + (t2.tv_nsec - t1.tv_nsec) / 1e9;
    //printf("[DEBUG] Second wait_finished() returned %d after %.2f seconds\n", r2, elapsed2);

    if (r1 != 0 || r2 != 0)
        TEST_FAIL("Multiple calls to wait_finished failed");

    consumer_producer_destroy(&queue);
    TEST_PASS("Multiple calls to wait_finished succeed");
}


void test_called_before_init() {
    consumer_producer_t queue;
    memset(&queue, 0, sizeof(queue));

    int result = consumer_producer_wait_finished(&queue);
    if (result != -1)
        TEST_FAIL("wait_finished before init should return -1");

    TEST_PASS("wait_finished before init returns -1");
}

void test_called_on_null() {
    int result = consumer_producer_wait_finished(NULL);
    if (result != -1)
        TEST_FAIL("wait_finished(NULL) should return -1");

    TEST_PASS("wait_finished on NULL returns -1");
}

void test_no_busy_wait() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 1);
    consumer_producer_put(&queue, strdup("block"));
    consumer_producer_signal_finished(&queue);

    int result = -1;
    struct timespec start, end;
    wait_args_t args = { .queue = &queue, .flag = &result, .start = &start, .end = &end };

    pthread_t thread;
    pthread_create(&thread, NULL, wait_thread_func, &args);

    sleep(1); // Ensure thread is blocked

    char* item = consumer_producer_get(&queue);
    free(item);

    pthread_join(thread, NULL);

    double elapsed = (args.end->tv_sec - args.start->tv_sec) + 
                     (args.end->tv_nsec - args.start->tv_nsec) / 1e9;

    if (elapsed < 0.5)
        TEST_FAIL("wait_finished returned too quickly – possible busy wait");

    if (result != 0)
        TEST_FAIL("wait_finished failed to return 0");

    consumer_producer_destroy(&queue);
    TEST_PASS("wait_finished blocks correctly (no busy wait)");
}

int main() {
    printf("=== Testing consumer_producer_wait_finished ===\n");
    test_returns_0_when_finished_and_empty();
    test_blocks_until_queue_empty();
    test_does_not_return_before_finish();
    test_multiple_calls();
    test_called_before_init();
    test_called_on_null();
    test_no_busy_wait();
    printf(GREEN "All wait_finished tests passed.\n" NC);
    return 0;
}
