#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "../../plugins/sync/monitor.h"

#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define NC "\033[0m" // No Color

/*
 * Thread function to test waiting for signal
 */
void* wait_for_signal_thread(void* arg) {
    monitor_t* monitor = (monitor_t*)arg;
    int res = monitor_wait(monitor);
    if (res == 0) {
        printf(GREEN "[PASS] Thread woke up successfully after signal.\n" NC);
    } else {
        printf(RED "[FAIL] Thread did not wake up as expected after signal.\n" NC);
    }
    return NULL;
}

/*
 * Test 1: monitor_wait with NULL pointer
 * Expected: return -1
 */
void test_monitor_wait_null() {
    printf("[TEST] test_monitor_wait_null...\n");
    int res = monitor_wait(NULL);
    if (res == -1) {
        printf(GREEN "[PASS] test_monitor_wait_null passed successfully.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_wait_null: Expected -1 when passing NULL pointer.\n" NC);
    }
}

/*
 * Test 2: monitor_wait on uninitialized monitor
 * Expected: return -1
 */
void test_monitor_wait_uninitialized() {
    printf("[TEST] test_monitor_wait_uninitialized...\n");
    monitor_t monitor;
    monitor.initialized = 0;
    int res = monitor_wait(&monitor);
    if (res == -1) {
        printf(GREEN "[PASS] test_monitor_wait_uninitialized passed successfully.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_wait_uninitialized: Expected -1 when monitor is not initialized.\n" NC);
    }
}

/*
 * Test 3: Signal before wait
 * Expected: wait returns immediately with 0
 */
void test_monitor_wait_signal_before_wait() {
    printf("[TEST] test_monitor_wait_signal_before_wait...\n");
    monitor_t monitor;
    monitor_init(&monitor);
    monitor_signal(&monitor);
    int res = monitor_wait(&monitor);
    if (res == 0) {
        printf(GREEN "[PASS] test_monitor_wait_signal_before_wait passed successfully.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_wait_signal_before_wait: Wait did not return immediately after signal.\n" NC);
    }
    monitor_destroy(&monitor);
}

/*
 * Test 4: Wait before signal (multi-thread)
 * Expected: thread blocks until signal, then returns 0
 */
void test_monitor_wait_before_signal() {
    printf("[TEST] test_monitor_wait_before_signal...\n");
    monitor_t monitor;
    pthread_t tid;

    monitor_init(&monitor);
    pthread_create(&tid, NULL, wait_for_signal_thread, &monitor);
    usleep(100000); // Allow thread to enter wait

    monitor_signal(&monitor);

    pthread_join(tid, NULL);
    monitor_destroy(&monitor);
}

/*
 * Test 5: Multiple threads waiting
 * Expected: all threads wake up after one broadcast
 */
void* multi_wait_thread(void* arg) {
    monitor_t* monitor = (monitor_t*)arg;
    int res = monitor_wait(monitor);
    if (res == 0) {
        printf(GREEN "[PASS] A waiting thread woke up successfully.\n" NC);
    } else {
        printf(RED "[FAIL] A waiting thread failed to wake up.\n" NC);
    }
    return NULL;
}

void test_monitor_wait_multiple_threads() {
    printf("[TEST] test_monitor_wait_multiple_threads...\n");
    monitor_t monitor;
    pthread_t t1, t2, t3;

    monitor_init(&monitor);
    pthread_create(&t1, NULL, multi_wait_thread, &monitor);
    pthread_create(&t2, NULL, multi_wait_thread, &monitor);
    pthread_create(&t3, NULL, multi_wait_thread, &monitor);

    usleep(200000); // Allow all threads to enter wait
    monitor_signal(&monitor);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);

    monitor_destroy(&monitor);
}

/*
 * Test 6: Wait after destroy
 * Expected: return -1
 */
void test_monitor_wait_after_destroy() {
    printf("[TEST] test_monitor_wait_after_destroy...\n");
    monitor_t monitor;
    monitor_init(&monitor);
    monitor_destroy(&monitor);
    int res = monitor_wait(&monitor);
    if (res == -1) {
        printf(GREEN "[PASS] test_monitor_wait_after_destroy passed successfully.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_wait_after_destroy: Expected -1 after destroy.\n" NC);
    }
}

/*
 * MAIN FUNCTION TO RUN ALL WAIT TESTS
 */
int main() {
    printf("=== Running monitor_wait unit tests ===\n");
    test_monitor_wait_null();
    test_monitor_wait_uninitialized();
    test_monitor_wait_signal_before_wait();
    test_monitor_wait_before_signal();
    test_monitor_wait_multiple_threads();
    test_monitor_wait_after_destroy();
    printf(GREEN "✅ All monitor_wait tests finished.\n" NC);
    return 0;
}
