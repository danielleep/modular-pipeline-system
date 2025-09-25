#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include "../../plugins/sync/monitor.h"

#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define NC "\033[0m" // No Color

#define TIMEOUT 2 // seconds for timeout checks

/*
 * Utility for test output
 */
#define PRINT_PASS(test_name) printf(GREEN "[PASS] %s\n" NC, test_name)
#define PRINT_FAIL(test_name, message) printf(RED "[FAIL] %s: %s\n" NC, test_name, message)

/*
 * ========================
 *   THREAD FUNCTIONS
 * ========================
 */

typedef struct {
    monitor_t *monitor;
    int woke_up;
} thread_data_t;

void* wait_thread_func(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    int res = monitor_wait(data->monitor);
    data->woke_up = (res == 0);
    return NULL;
}

/*
 * ========================
 *   TEST CASES
 * ========================
 */

/* Test 1: Basic Wait -> Signal */
void test_wait_then_signal() {
    monitor_t monitor;
    monitor_init(&monitor);
    thread_data_t td = { .monitor = &monitor, .woke_up = 0 };
    pthread_t t;
    pthread_create(&t, NULL, wait_thread_func, &td);
    usleep(100000); // Allow thread to enter wait
    monitor_signal(&monitor);
    pthread_join(t, NULL);
    if (td.woke_up) PRINT_PASS("test_wait_then_signal");
    else PRINT_FAIL("test_wait_then_signal", "Thread did not wake up after signal");
    monitor_destroy(&monitor);
}

/* Test 2: Signal before Wait (state remembered) */
void test_signal_before_wait() {
    monitor_t monitor;
    monitor_init(&monitor);
    monitor_signal(&monitor);
    int res = monitor_wait(&monitor);
    if (res == 0) PRINT_PASS("test_signal_before_wait");
    else PRINT_FAIL("test_signal_before_wait", "Thread did not return immediately after previous signal");
    monitor_destroy(&monitor);
}

/* Test 3: Multiple threads waiting -> One signal (broadcast) */
void test_multiple_threads_wait_broadcast() {
    monitor_t monitor;
    monitor_init(&monitor);

    int n = 3;
    pthread_t threads[n];
    thread_data_t td[n];

    for (int i = 0; i < n; i++) {
        td[i].monitor = &monitor;
        td[i].woke_up = 0;
        pthread_create(&threads[i], NULL, wait_thread_func, &td[i]);
    }
    usleep(200000); // Allow all threads to enter wait
    monitor_signal(&monitor);

    int woke_all = 1;
    for (int i = 0; i < n; i++) {
        pthread_join(threads[i], NULL);
        if (!td[i].woke_up) woke_all = 0;
    }
    if (woke_all) PRINT_PASS("test_multiple_threads_wait_broadcast");
    else PRINT_FAIL("test_multiple_threads_wait_broadcast", "Not all threads woke up after signal");

    monitor_destroy(&monitor);
}

/* Test 4: Signal -> Reset -> Wait */
void test_signal_reset_wait() {
    monitor_t monitor;
    monitor_init(&monitor);
    monitor_signal(&monitor);
    monitor_reset(&monitor);

    pthread_t t;
    thread_data_t td = { .monitor = &monitor, .woke_up = 0 };
    pthread_create(&t, NULL, wait_thread_func, &td);
    usleep(100000); // Ensure thread is waiting
    if (td.woke_up) {
        PRINT_FAIL("test_signal_reset_wait", "Thread woke up unexpectedly after reset");
    } else {
        monitor_signal(&monitor);
        pthread_join(t, NULL);
        if (td.woke_up) PRINT_PASS("test_signal_reset_wait");
        else PRINT_FAIL("test_signal_reset_wait", "Thread did not wake up after new signal");
    }
    monitor_destroy(&monitor);
}

/* Test 5: Continuous use of monitor (Producer-Consumer style) */
void test_continuous_use() {
    monitor_t monitor;
    monitor_init(&monitor);
    int iterations = 5;
    int pass = 1;

    for (int i = 0; i < iterations; i++) {
        pthread_t t;
        thread_data_t td = { .monitor = &monitor, .woke_up = 0 };
        pthread_create(&t, NULL, wait_thread_func, &td);
        usleep(100000); // Allow wait
        monitor_signal(&monitor);
        pthread_join(t, NULL);
        if (!td.woke_up) {
            pass = 0;
            break;
        }
        monitor_reset(&monitor);
    }

    if (pass) PRINT_PASS("test_continuous_use");
    else PRINT_FAIL("test_continuous_use", "One of the iterations failed");
    monitor_destroy(&monitor);
}


/*
 * Test 6 (Enhanced): Destroy while thread is waiting (stability test with debug)
 */
void* debug_wait_thread(void* arg) {
    monitor_t* monitor = (monitor_t*)arg;
    printf("[DEBUG] [Thread] Started, calling monitor_wait...\n");
    int res = monitor_wait(monitor);
    printf("[DEBUG] [Thread] monitor_wait returned: %d\n", res);
    return NULL;
}


/* Test 7: Multiple init/destroy cycles */
void test_multiple_init_destroy_cycles() {
    monitor_t monitor;
    int cycles = 3;
    int pass = 1;
    for (int i = 0; i < cycles; i++) {
        if (monitor_init(&monitor) != 0) { pass = 0; break; }
        monitor_destroy(&monitor);
    }
    if (pass) PRINT_PASS("test_multiple_init_destroy_cycles");
    else PRINT_FAIL("test_multiple_init_destroy_cycles", "Failed during re-init cycles");
}

/* Test 8: Signal twice before wait */
void test_signal_twice_before_wait() {
    monitor_t monitor;
    monitor_init(&monitor);
    monitor_signal(&monitor);
    monitor_signal(&monitor);
    int res = monitor_wait(&monitor);
    if (res == 0) PRINT_PASS("test_signal_twice_before_wait");
    else PRINT_FAIL("test_signal_twice_before_wait", "Thread did not wake up despite multiple signals");
    monitor_destroy(&monitor);
}

/* Test 9: Zero waiters + signal + wait */
void test_signal_no_waiters_then_wait() {
    monitor_t monitor;
    monitor_init(&monitor);
    monitor_signal(&monitor);
    sleep(1);
    int res = monitor_wait(&monitor);
    if (res == 0) PRINT_PASS("test_signal_no_waiters_then_wait");
    else PRINT_FAIL("test_signal_no_waiters_then_wait", "Wait did not return immediately after signal");
    monitor_destroy(&monitor);
}

/* Test 10: Stress test with many threads */
void test_stress_multiple_threads() {
    monitor_t monitor;
    monitor_init(&monitor);

    int n = 20;
    pthread_t threads[n];
    thread_data_t td[n];

    for (int i = 0; i < n; i++) {
        td[i].monitor = &monitor;
        td[i].woke_up = 0;
        pthread_create(&threads[i], NULL, wait_thread_func, &td[i]);
    }
    usleep(300000); // Allow all threads to enter wait
    monitor_signal(&monitor);

    int woke_all = 1;
    for (int i = 0; i < n; i++) {
        pthread_join(threads[i], NULL);
        if (!td[i].woke_up) woke_all = 0;
    }
    if (woke_all) PRINT_PASS("test_stress_multiple_threads");
    else PRINT_FAIL("test_stress_multiple_threads", "Not all threads woke up in stress test");

    monitor_destroy(&monitor);
}

/*
 * ========================
 *   MAIN FUNCTION
 * ========================
 */
int main() {
    printf("=== Running Monitor Integration Tests ===\n");
    test_wait_then_signal();
    test_signal_before_wait();
    test_multiple_threads_wait_broadcast();
    test_signal_reset_wait();
    test_continuous_use();
    test_multiple_init_destroy_cycles();
    test_signal_twice_before_wait();
    test_signal_no_waiters_then_wait();
    test_stress_multiple_threads();
    printf(GREEN "✅ All integration tests finished.\n" NC);
    return 0;
}
