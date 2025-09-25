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

void test_valid_initialization() {
    consumer_producer_t queue;
    const char* err = consumer_producer_init(&queue, 5);
    if (err != NULL)
        TEST_FAIL("Valid initialization returned error");

    if (queue.capacity != 5 || queue.count != 0 || queue.head != 0 || queue.tail != 0)
        TEST_FAIL("Queue fields not initialized properly");

    if (!queue.initialized)
        TEST_FAIL("Queue should be marked as initialized");

    if (!queue.not_empty_monitor.initialized || !queue.not_full_monitor.initialized || !queue.finished_monitor.initialized)
        TEST_FAIL("One or more monitors not initialized properly");

    consumer_producer_destroy(&queue);
    TEST_PASS("Valid initialization");
}

void test_zero_capacity() {
    consumer_producer_t queue;
    const char* err = consumer_producer_init(&queue, 0);
    if (err == NULL)
        TEST_FAIL("Initialization with 0 capacity should fail");

    TEST_PASS("Initialization with 0 capacity failed as expected");
}

void test_negative_capacity() {
    consumer_producer_t queue;
    const char* err = consumer_producer_init(&queue, -1);
    if (err == NULL)
        TEST_FAIL("Initialization with negative capacity should fail");

    TEST_PASS("Initialization with negative capacity failed as expected");
}

void test_double_initialization() {
    consumer_producer_t queue;
    const char* err1 = consumer_producer_init(&queue, 3);
    if (err1 != NULL)
        TEST_FAIL("First initialization failed unexpectedly");

    const char* err2 = consumer_producer_init(&queue, 3);
    if (err2 == NULL)
        TEST_FAIL("Double initialization should fail");

    consumer_producer_destroy(&queue);
    TEST_PASS("Double initialization fails as expected");
}

int main() {
    printf("=== Testing consumer_producer_init ===\n");
    test_valid_initialization();
    test_zero_capacity();
    test_negative_capacity();
    test_double_initialization();
    printf(GREEN "All init tests passed.\n" NC);
    return 0;
}
