#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "../../plugins/sync/monitor.h"

#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define NC "\033[0m" // No Color

/*
 * Thread function for wait test
 */
void* wait_for_signal(void* arg) {
    monitor_t* monitor = (monitor_t*)arg;
    int res = monitor_wait(monitor);
    if (res == 0) {
        printf(GREEN "[PASS] test_monitor_signal_thread_wait: Thread successfully woke up after signal.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_signal_thread_wait: Thread did not wake up as expected.\n" NC);
    }
    return NULL;
}

/*
 * Test 1: Calling monitor_signal with NULL pointer
 * Expected: no crash
 */
void test_monitor_signal_null() {
    printf("[TEST] test_monitor_signal_null...\n");
    monitor_signal(NULL);
    printf(GREEN "[PASS] test_monitor_signal_null passed successfully.\n" NC);
}

/*
 * Test 2: Calling monitor_signal on uninitialized monitor
 * Expected: no crash
 */
void test_monitor_signal_uninitialized() {
    printf("[TEST] test_monitor_signal_uninitialized...\n");
    monitor_t monitor;
    monitor.initialized = 0;
    monitor_signal(&monitor);
    printf(GREEN "[PASS] test_monitor_signal_uninitialized passed successfully.\n" NC);
}

/*
 * Test 3: Sending signal after init
 * Expected: signaled = 1
 */
void test_monitor_signal_after_init() {
    printf("[TEST] test_monitor_signal_after_init...\n");
    monitor_t monitor;
    monitor_init(&monitor);
    monitor_signal(&monitor);
    if (monitor.signaled == 1) {
        printf(GREEN "[PASS] test_monitor_signal_after_init passed successfully.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_signal_after_init: signaled flag not set to 1 after signal.\n" NC);
    }
    monitor_destroy(&monitor);
}

/*
 * Test 4: Sending signal twice in a row
 * Expected: signaled stays 1
 */
void test_monitor_signal_double() {
    printf("[TEST] test_monitor_signal_double...\n");
    monitor_t monitor;
    monitor_init(&monitor);
    monitor_signal(&monitor);
    monitor_signal(&monitor);
    if (monitor.signaled == 1) {
        printf(GREEN "[PASS] test_monitor_signal_double passed successfully.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_signal_double: signaled flag changed unexpectedly.\n" NC);
    }
    monitor_destroy(&monitor);
}

/*
 * Test 5: Signal before wait
 * Expected: wait should return immediately since signal was sent
 */
void test_monitor_signal_before_wait() {
    printf("[TEST] test_monitor_signal_before_wait...\n");
    monitor_t monitor;
    monitor_init(&monitor);
    monitor_signal(&monitor);
    int res = monitor_wait(&monitor);
    if (res == 0) {
        printf(GREEN "[PASS] test_monitor_signal_before_wait passed successfully.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_signal_before_wait: wait did not return immediately after signal.\n" NC);
    }
    monitor_destroy(&monitor);
}

/*
 * Test 6: Signal wakes up waiting thread (multi-thread test)
 */
void test_monitor_signal_wakes_waiting_thread() {
    printf("[TEST] test_monitor_signal_wakes_waiting_thread...\n");
    monitor_t monitor;
    pthread_t tid;

    monitor_init(&monitor);
    pthread_create(&tid, NULL, wait_for_signal, &monitor);
    usleep(100000); // Give time for the thread to start waiting

    monitor_signal(&monitor);

    pthread_join(tid, NULL);
    monitor_destroy(&monitor);
}

/*
 * Test 7: Signal after destroy
 * Expected: no crash
 */
void test_monitor_signal_after_destroy() {
    printf("[TEST] test_monitor_signal_after_destroy...\n");
    monitor_t monitor;
    monitor_init(&monitor);
    monitor_destroy(&monitor);
    monitor_signal(&monitor); // Should not crash
    printf(GREEN "[PASS] test_monitor_signal_after_destroy passed successfully.\n" NC);
}

/*
 * MAIN FUNCTION TO RUN ALL SIGNAL TESTS
 */
int main() {
    printf("=== Running monitor_signal unit tests ===\n");
    test_monitor_signal_null();
    test_monitor_signal_uninitialized();
    test_monitor_signal_after_init();
    test_monitor_signal_double();
    test_monitor_signal_before_wait();
    test_monitor_signal_wakes_waiting_thread();
    test_monitor_signal_after_destroy();
    printf(GREEN "✅ All monitor_signal tests finished.\n" NC);
    return 0;
}
