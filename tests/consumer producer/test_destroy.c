#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../plugins/sync/consumer_producer.h"

// Terminal colors
#define RED     "\033[0;31m"
#define GREEN   "\033[0;32m"
#define NC      "\033[0m"

#define TEST_PASS(msg) printf(GREEN "[PASS] " NC msg "\n")
#define TEST_FAIL(msg) do { printf(RED "[FAIL] " NC msg "\n"); exit(1); } while(0)

void test_destroy_after_init() {
    consumer_producer_t queue;
    const char* err = consumer_producer_init(&queue, 3);
    if (err != NULL)
        TEST_FAIL("Failed to initialize queue");

    consumer_producer_destroy(&queue);

    if (queue.items != NULL)
        TEST_FAIL("Items not freed after destroy");

    if (queue.initialized != 0)
        TEST_FAIL("Queue still marked as initialized after destroy");

    TEST_PASS("Destroy after init works correctly");
}

void test_double_destroy() {
    consumer_producer_t queue;
    const char* err = consumer_producer_init(&queue, 2);
    if (err != NULL)
        TEST_FAIL("Initialization failed");

    consumer_producer_destroy(&queue);
    // second call shouldn't crash
    consumer_producer_destroy(&queue);

    TEST_PASS("Double destroy doesn't crash");
}

void test_destroy_without_init() {
    consumer_producer_t queue;
    memset(&queue, 0, sizeof(queue)); // ensure clean state
    consumer_producer_destroy(&queue); // should not crash

    TEST_PASS("Destroy without init doesn't crash");
}

void test_partial_init_destroy() {
    consumer_producer_t queue;
    memset(&queue, 0, sizeof(queue));
    monitor_init(&queue.not_empty_monitor); // partial init
    queue.initialized = 0;

    consumer_producer_destroy(&queue); // should still clean safely

    TEST_PASS("Destroy after partial init doesn't crash");
}

int main() {
    printf("=== Testing consumer_producer_destroy ===\n");
    test_destroy_after_init();
    test_double_destroy();
    test_destroy_without_init();
    test_partial_init_destroy();
    printf(GREEN "All destroy tests passed.\n" NC);
    return 0;
}
