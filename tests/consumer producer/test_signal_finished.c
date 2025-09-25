#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "../../plugins/sync/consumer_producer.h"

// Output colors
#define RED     "\033[0;31m"
#define GREEN   "\033[0;32m"
#define NC      "\033[0m"

#define TEST_PASS(msg) printf(GREEN "[PASS] " NC msg "\n")
#define TEST_FAIL(msg) do { printf(RED "[FAIL] " NC msg "\n"); exit(1); } while(0)


// SPECIAL TEST — specific to Daniel's implementation
void test_flag_is_set_after_signal() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 2);

    consumer_producer_signal_finished(&queue);

    if (queue.finished_flag != 1)
        TEST_FAIL("Special for Danielle: finished_flag was not set after signal_finished()");

    consumer_producer_destroy(&queue);
    TEST_PASS("Special for Danielle: finished_flag set after signal_finished()");
}


void test_put_fails_after_finish() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 1);
    consumer_producer_signal_finished(&queue);

    const char* err = consumer_producer_put(&queue, strdup("X"));
    if (err == NULL)
        TEST_FAIL("Put succeeded after signal_finished");

    consumer_producer_destroy(&queue);
    TEST_PASS("Put fails after signal_finished");
}


void test_get_continues_with_existing_items() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 2);

    consumer_producer_put(&queue, strdup("A"));
    consumer_producer_put(&queue, strdup("B"));

    consumer_producer_signal_finished(&queue);

    char* r1 = consumer_producer_get(&queue);
    char* r2 = consumer_producer_get(&queue);

    if (!r1 || strcmp(r1, "A") != 0 || !r2 || strcmp(r2, "B") != 0)
        TEST_FAIL("Get failed to return items after signal_finished");

    free(r1);
    free(r2);
    consumer_producer_destroy(&queue);
    TEST_PASS("Get continues correctly after signal_finished");
}


void test_get_returns_null_when_empty_after_finish() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 1);
    consumer_producer_signal_finished(&queue);

    char* r = consumer_producer_get(&queue);
    if (r != NULL)
        TEST_FAIL("Get returned value on empty queue after signal_finished");

    consumer_producer_destroy(&queue);
    TEST_PASS("Get returns NULL when empty after signal_finished");
}


typedef struct {
    consumer_producer_t* queue;
    int* wait_done_flag;
} wait_args_t;

void* wait_thread_func(void* arg) {
    wait_args_t* a = (wait_args_t*)arg;
    consumer_producer_wait_finished(a->queue);
    *(a->wait_done_flag) = 1;
    return NULL;
}

void test_wait_finished_returns_only_after_empty() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 1);
    consumer_producer_put(&queue, strdup("X"));
    consumer_producer_signal_finished(&queue);

    int done = 0;
    wait_args_t args = { .queue = &queue, .wait_done_flag = &done };
    pthread_t thread;
    pthread_create(&thread, NULL, wait_thread_func, &args);

    sleep(1);
    if (done)
        TEST_FAIL("wait_finished returned before queue was empty");

    char* item = consumer_producer_get(&queue);
    free(item);

    pthread_join(thread, NULL);
    if (!done)
        TEST_FAIL("wait_finished did not return after queue became empty");

    consumer_producer_destroy(&queue);
    TEST_PASS("wait_finished blocks until queue is empty");
}


void test_wait_finished_returns_immediately_when_empty() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 1);
    consumer_producer_signal_finished(&queue);

    consumer_producer_wait_finished(&queue);  // should return immediately

    consumer_producer_destroy(&queue);
    TEST_PASS("wait_finished returns immediately when queue is empty");
}


void test_multiple_calls_to_signal_finished() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 1);

    consumer_producer_signal_finished(&queue);
    consumer_producer_signal_finished(&queue); // second call

    if (queue.finished_flag != 1)
        TEST_FAIL("finished_flag changed unexpectedly on second call");

    consumer_producer_destroy(&queue);
    TEST_PASS("Multiple calls to signal_finished handled correctly");
}


void test_signal_before_init() {
    consumer_producer_t queue;
    memset(&queue, 0, sizeof(queue));

    // Should not crash
    consumer_producer_signal_finished(&queue);

    TEST_PASS("signal_finished before init does not crash");
}

void test_signal_on_null_queue() {
    // Should not crash
    consumer_producer_signal_finished(NULL);
    TEST_PASS("signal_finished with NULL does not crash");
}


int main() {
    printf("=== Testing consumer_producer_signal_finished ===\n");
    test_flag_is_set_after_signal();  // special test
    test_put_fails_after_finish();
    test_get_continues_with_existing_items();
    test_get_returns_null_when_empty_after_finish();
    test_wait_finished_returns_only_after_empty();
    test_wait_finished_returns_immediately_when_empty();
    test_multiple_calls_to_signal_finished();
    test_signal_before_init();
    test_signal_on_null_queue();
    printf(GREEN "All signal_finished tests passed.\n" NC);
    return 0;
}
